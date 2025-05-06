# Box Strategy Trading Application

A C++ application for identifying and trading box spreads using the Zerodha Kite Connect API.

## Option Chain Access

This application implements efficient access to option chain data from the Zerodha Kite Connect API:

1. **InstrumentModel** - Enhanced with better handling of option types and instrument classification
2. **MarketDataManager** - Added methods for retrieving and filtering option chains:
   - `getOptionChain` - Retrieves all options for a specific underlying and expiry
   - `getOptionChainWithQuotes` - Retrieves option chain with live market quotes
3. **Rate Limiting** - Implemented intelligent rate limiting to handle API restrictions:
   - Configurable rate limits per endpoint
   - Automatic backoff when rate limits are exceeded
   - Adaptive adjustment when receiving 429 "Too Many Requests" errors
   - Instruments caching to reduce API calls

## Rate Limit Handling

The Zerodha Kite Connect API imposes strict rate limits. This application includes strategies to handle them:

1. **Per-Endpoint Rate Limits** - Different API endpoints have different rate limits:
   - `/instruments`: 1 request per minute
   - `/quote`, `/quote/ltp`, `/quote/ohlc`: 15 requests per minute
   - Other endpoints: 30 requests per minute

2. **Data Caching** - The application caches instrument data to minimize API calls:
   - Instruments are cached with a configurable TTL (default: 30 minutes)
   - Expiry data is cached for reuse across calculations

3. **Backoff Strategy** - When rate limits are exceeded:
   - The application automatically waits until the rate limit window resets
   - Rate limits are automatically adjusted down when 429 errors are received

## Recommendations for API Usage

To effectively use the Zerodha Kite Connect API with this application:

1. **Increase Scan Interval** - Set a higher `strategy/scan_interval_seconds` in config.json (60+ seconds recommended)
2. **Conservative Rate Limits** - Use conservative settings in the `api/rate_limits` section
3. **Instrument Caching** - Increase `api/instruments_cache_ttl_minutes` for longer caching (up to 60 minutes)
4. **Focus on Specific Expiries** - Limit the expiry range with `expiry/max_count` to reduce API calls
5. **Offline Mode** - Implement a data recording and replay mechanism for development/testing

## Running the Application

```bash
# Build the application
mkdir -p build && cd build
cmake ..
make -j8

# Run the application
./box_strategy
```

## Configuration

The application is configured using `config.json`. Key settings:

```json
{
  "api": {
    "instruments_cache_ttl_minutes": 30,
    "rate_limits": {
      "default": 30,
      "instruments": 1,
      "ltp": 15,
      "ohlc": 15,
      "quote": 15
    }
  },
  "strategy": {
    "scan_interval_seconds": 60
  }
}
``` 