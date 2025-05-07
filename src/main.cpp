/**
 * @file main.cpp
 * @brief Main entry point for the box strategy HFT application
 */

#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>

#include "utils/Logger.hpp"
#include "utils/HttpClient.hpp"
#include "utils/ThreadPool.hpp"
#include "utils/ThreadPoolOptimizer.hpp"
#include "config/ConfigManager.hpp"
#include "auth/AuthManager.hpp"
#include "market/MarketDataManager.hpp"
#include "market/ExpiryManager.hpp"
#include "analysis/CombinationAnalyzer.hpp"
#include "analysis/MarketDepthAnalyzer.hpp"
#include "risk/RiskCalculator.hpp"
#include "risk/FeeCalculator.hpp"
#include "trading/OrderManager.hpp"
#include "trading/PaperTrader.hpp"
#include "models/BoxSpreadModel.hpp"

using namespace BoxStrategy;

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};

// Signal handler for graceful shutdown
void signalHandler(int signum) {
    std::cout << "Interrupt signal (" << signum << ") received. Shutting down gracefully..." << std::endl;
    g_running = false;
}

int main(int argc, char* argv[]) {
    // Register signal handler
    std::signal(SIGINT, signalHandler);  // Ctrl+C
    std::signal(SIGTERM, signalHandler); // Termination request
    
    try {
        // Configuration file path
        std::string configPath = "config.json";
        
        // Override config path from command line if provided
        if (argc > 1) {
            configPath = argv[1];
        }
        
        // Create logger
        auto logger = std::make_shared<Logger>("box_strategy.log", true, LogLevel::INFO);
        logger->info("Starting Box Strategy HFT application");
        
        // Create configuration manager
        auto configManager = std::make_shared<ConfigManager>(configPath, logger);
        if (!configManager->loadConfig()) {
            logger->fatal("Failed to load configuration from {}", configPath);
            return 1;
        }
        
        // Get configuration values
        std::string underlying = configManager->getStringValue("strategy/underlying", "NIFTY");
        std::string exchange = configManager->getStringValue("strategy/exchange", "NFO");
        uint64_t quantity = configManager->getIntValue("strategy/quantity", 1);
        int maxExpiries = configManager->getIntValue("expiry/max_count", 3);
        int numThreads = configManager->getIntValue("system/num_threads", 4);
        bool isPaperTrading = configManager->getBoolValue("strategy/paper_trading", true);
        int scanIntervalSeconds = configManager->getIntValue("strategy/scan_interval_seconds", 60);
        
        logger->info("Configuration loaded. Underlying: {}, Exchange: {}, Quantity: {}", 
                   underlying, exchange, quantity);
        
        // Create thread pool
        auto threadPool = std::make_shared<ThreadPool>(numThreads, logger);
        logger->info("Thread pool initialized with {} threads", numThreads);
        
        // Create thread pool optimizer
        auto threadPoolOptimizer = std::make_shared<ThreadPoolOptimizer>(threadPool, logger);
        
        // Create HTTP client
        auto httpClient = std::make_shared<HttpClient>(logger);
        
        // Create authentication manager
        auto authManager = std::make_shared<AuthManager>(configManager, httpClient, logger);
        
        // Check if we need to authenticate
        if (!authManager->isAccessTokenValid()) {
            logger->info("Access token is not valid. Please authenticate.");
            
            // Generate login URL
            std::string loginUrl = authManager->generateLoginUrl();
            std::cout << "Please open the following URL in your browser and complete the login process:" << std::endl;
            std::cout << loginUrl << std::endl;
            
            // Wait for request token from user
            std::string requestToken;
            std::cout << "Enter the request token: ";
            std::cin >> requestToken;
            
            // Generate access token
            if (!authManager->generateAccessToken(requestToken)) {
                logger->fatal("Failed to generate access token");
                return 1;
            }
            
            logger->info("Authentication successful");
        } else {
            logger->info("Using existing access token");
        }
        
        // Initialize MarketDataManager
        std::shared_ptr<MarketDataManager> marketDataManager = std::make_shared<MarketDataManager>(
            authManager, httpClient, logger, configManager);
        
        // Create expiry manager
        auto expiryManager = std::make_shared<ExpiryManager>(configManager, marketDataManager, logger);
        
        // Create fee calculator
        auto feeCalculator = std::make_shared<FeeCalculator>(configManager, logger);
        
        // Create risk calculator
        auto riskCalculator = std::make_shared<RiskCalculator>(configManager, logger);
        
        // Create market depth analyzer
        auto marketDepthAnalyzer = std::make_shared<MarketDepthAnalyzer>(configManager, marketDataManager, logger);
        
        // Initialize combination analyzer
        logger->info("Initializing CombinationAnalyzer");
        auto combinationAnalyzer = std::make_shared<CombinationAnalyzer>(
            configManager, marketDataManager, expiryManager, 
            feeCalculator, riskCalculator, threadPool, logger);
        
        // Set the thread pool optimizer on the combination analyzer
        combinationAnalyzer->setThreadPoolOptimizer(threadPoolOptimizer);
        
        // Create order manager
        auto orderManager = std::make_shared<OrderManager>(configManager, authManager, httpClient, logger);
        
        // Create paper trader
        auto paperTrader = std::make_shared<PaperTrader>(configManager, marketDataManager, logger);
        
        logger->info("All components initialized");
        
        // Test option chain functionality
        if (configManager->getBoolValue("debug/test_option_chain", false)) {
            logger->info("Testing option chain functionality");
            
            try {
                // Get underlying and exchange from config
                std::string testUnderlying = configManager->getStringValue("test/underlying", underlying);
                std::string testExchange = configManager->getStringValue("test/exchange", exchange);
                
                // Get available expiries
                auto expiriesFuture = expiryManager->refreshExpiries(testUnderlying, testExchange);
                auto expiries = expiriesFuture;
                
                if (!expiries.empty()) {
                    logger->info("Found {} expiries for {}", expiries.size(), testUnderlying);
                    
                    // Get option chain for the nearest expiry
                    auto nearestExpiry = expiries[0];
                    logger->info("Getting option chain for {} with expiry {}", 
                                testUnderlying, InstrumentModel::formatDate(nearestExpiry));
                    
                    // Get spot price to calculate strike price range
                    double spotPrice = 0.0;
                    try {
                        auto spotInstrumentFuture = marketDataManager->getInstrumentBySymbol(testUnderlying, "NSE");
                        auto spotInstrument = spotInstrumentFuture.get();
                        
                        if (spotInstrument.instrumentToken != 0) {
                            auto ltpFuture = marketDataManager->getLTP(spotInstrument.instrumentToken);
                            spotPrice = ltpFuture.get();
                            logger->info("Current spot price for {}: {}", testUnderlying, spotPrice);
                        }
                    } catch (const std::exception& e) {
                        logger->warn("Failed to get spot price: {}", e.what());
                    }
                    
                    // Calculate strike price range (20% above and below spot)
                    double minStrike = spotPrice * 0.8;
                    double maxStrike = spotPrice * 1.2;
                    
                    if (spotPrice <= 0.0) {
                        // If we couldn't get spot price, don't filter by strike
                        minStrike = 0.0;
                        maxStrike = 0.0;
                    }
                    
                    // Get option chain
                    auto optionChainFuture = marketDataManager->getOptionChain(
                        testUnderlying, nearestExpiry, testExchange, minStrike, maxStrike);
                    auto optionChain = optionChainFuture.get();
                    
                    if (!optionChain.empty()) {
                        logger->info("Found {} options in the chain", optionChain.size());
                        
                        // Print a sample of the option chain
                        size_t sampleSize = std::min(size_t(10), optionChain.size());
                        for (size_t i = 0; i < sampleSize; i++) {
                            const auto& option = optionChain[i];
                            logger->info("Option: {} ({}), Strike: {}, Type: {}", 
                                        option.tradingSymbol, 
                                        option.instrumentToken,
                                        option.strikePrice, 
                                        InstrumentModel::optionTypeToString(option.optionType));
                        }
                        
                        // Optionally get live quotes for the options
                        if (configManager->getBoolValue("test/get_option_quotes", false)) {
                            logger->info("Getting live quotes for options");
                            
                            auto optionChainWithQuotesFuture = marketDataManager->getOptionChainWithQuotes(
                                testUnderlying, nearestExpiry, testExchange, minStrike, maxStrike);
                            auto optionChainWithQuotes = optionChainWithQuotesFuture.get();
                            
                            if (!optionChainWithQuotes.empty()) {
                                logger->info("Got quotes for {} options", optionChainWithQuotes.size());
                                
                                // Print a sample with live quotes
                                sampleSize = std::min(size_t(5), optionChainWithQuotes.size());
                                for (size_t i = 0; i < sampleSize; i++) {
                                    const auto& option = optionChainWithQuotes[i];
                                    logger->info("Option with quote: {}, Strike: {}, Type: {}, LTP: {}, Bid: {}, Ask: {}", 
                                                option.tradingSymbol, 
                                                option.strikePrice,
                                                InstrumentModel::optionTypeToString(option.optionType),
                                                option.lastPrice,
                                                (!option.buyDepth.empty() ? option.buyDepth[0].price : 0.0),
                                                (!option.sellDepth.empty() ? option.sellDepth[0].price : 0.0));
                                }
                            }
                        }
                    } else {
                        logger->warn("No options found in the chain");
                    }
                } else {
                    logger->warn("No expiries found for {}", testUnderlying);
                }
            } catch (const std::exception& e) {
                logger->error("Error testing option chain: {}", e.what());
            }
        }
        
        // Main trading loop
        logger->info("Starting main trading loop");
        
        while (g_running) {
            try {
                logger->info("Scanning for profitable box spreads");
                
                // Find profitable box spreads
                auto boxSpreads = combinationAnalyzer->findProfitableSpreads(underlying, exchange);
                
                if (boxSpreads.empty()) {
                    logger->info("No profitable box spreads found. Waiting for next scan...");
                } else {
                    logger->info("Found {} profitable box spreads", boxSpreads.size());
                    
                    // Filter by market depth
                    boxSpreads = marketDepthAnalyzer->filterByLiquidity(boxSpreads, quantity);
                    logger->info("{} box spreads have sufficient liquidity", boxSpreads.size());
                    
                    if (!boxSpreads.empty()) {
                        // Get the most profitable box spread
                        BoxSpreadModel bestBoxSpread = boxSpreads[0];
                        
                        logger->info("Selected box spread: {}", bestBoxSpread.id);
                        logger->info("Theoretical value: {}, Net premium: {}, ROI: {}%, Profitability: {}",
                                   bestBoxSpread.calculateTheoreticalValue(),
                                   bestBoxSpread.calculateNetPremium(),
                                   bestBoxSpread.roi,
                                   bestBoxSpread.profitability);
                        
                        // Execute the box spread
                        if (isPaperTrading) {
                            logger->info("Simulating box spread trade (paper trading mode)");
                            PaperTradeResult result = paperTrader->simulateBoxSpreadTrade(bestBoxSpread, quantity);
                            logger->info("Paper trade result: ID: {}, Profit: {}", result.id, result.profit);
                        } else {
                            logger->info("Executing box spread trade (live trading mode)");
                            bool orderPlaced = orderManager->placeBoxSpreadOrder(bestBoxSpread, quantity);
                            if (orderPlaced) {
                                logger->info("Box spread order placed successfully");
                                
                                // Wait for execution
                                bestBoxSpread = orderManager->waitForBoxSpreadExecution(bestBoxSpread, 300);
                                
                                if (bestBoxSpread.allLegsExecuted) {
                                    logger->info("Box spread order fully executed");
                                } else {
                                    logger->warn("Box spread order not fully executed within timeout");
                                }
                            } else {
                                logger->error("Failed to place box spread order");
                            }
                        }
                    }
                }
                
                // Wait for the next scan
                logger->info("Waiting {} seconds for next scan", scanIntervalSeconds);
                
                for (int i = 0; i < scanIntervalSeconds && g_running; ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            } catch (const std::exception& e) {
                logger->error("Exception in main trading loop: {}", e.what());
                
                // Wait a short time before retrying
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
        
        logger->info("Main trading loop terminated");
        
        // Print paper trading results if applicable
        if (isPaperTrading) {
            double totalProfit = paperTrader->getTotalProfitLoss();
            logger->info("Paper trading total profit/loss: {}", totalProfit);
        }
        
        logger->info("Box Strategy HFT application shutting down");
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}