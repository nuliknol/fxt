# PrintMoney.mq4 Expert Advisor

## Overview

`PrintMoney.mq4` is an Expert Advisor (EA) for MetaTrader 4 that implements a grid-based trading strategy. It is designed to automatically place trades based on a predefined grid, with the goal of capturing profits from market movements. This EA can be configured to trade in one direction only (either buying or selling).

## How it Works

The EA operates on a grid system. The grid is a series of price levels separated by a fixed number of pips, defined by the `grid_step_pips` parameter.

1.  **Initial State**: When the EA starts, it determines the current grid level based on the market price.
2.  **Grid Trading**: As the price moves, the EA will open a new trade (buy or sell, depending on the configuration) each time the price crosses a new grid line in the direction of the trend. For example, in a `BUY_ONLY` strategy, a new buy order is placed as the price moves up through consecutive grid lines.
3.  **Profit Locking**: The EA includes a mechanism to secure profits on individual orders. When an order becomes profitable by a certain amount (`lock_trigger_pips`), the EA sets a stop-loss to lock in a portion of the profit (`lock_profit_pips`).

## Parameters

-   `trade_direction`: The direction of trades. Can be `BUY_ONLY` or `SELL_ONLY`.
-   `lot_per_entry`: The lot size for each trade.
-   `grid_step_pips`: The distance in pips between grid lines.
-   `lock_trigger_pips`: The number of pips in profit required to trigger the profit-locking mechanism.
-   `lock_profit_pips`: The number of pips of profit to lock in with a stop-loss once the `lock_trigger_pips` threshold is reached.
-   `ea_enabled`: A master switch to enable or disable the EA's trading activity.
-   `magic_number`: A unique number to identify orders placed by this EA instance. This is crucial for preventing conflicts with other EAs.
-   `protect_manually_placed_orders`: If `true`, the EA will also manage the stop-loss for manual orders (with magic number 0) on the same symbol and trade direction.
-   `ignore_tickets_csv`: A comma-separated list of ticket numbers that the EA should ignore.
-   `max_gap_orders_per_tick`: The maximum number of grid levels the EA can open in a single tick. This prevents opening an excessive number of orders during fast market movements.
-   `timer_period_seconds`: The interval in seconds for the EA's periodic maintenance tasks, such as checking for profit-locking opportunities.
-   `log_level`: Controls the verbosity of the EA's logs in the Experts tab.

## `lock_trigger_pips` vs. `lock_profit_pips`

These two parameters work together to manage profit-taking for individual orders.

-   **`lock_trigger_pips`**: This is the **condition** for activating the profit-locking mechanism. It represents the minimum number of pips an order must be in profit before the EA will set a stop-loss. The profit is calculated from the grid line where the order was conceptually opened, not the exact fill price.

-   **`lock_profit_pips`**: This is the **level** at which profits are locked. Once an order's profit has reached `lock_trigger_pips`, the EA will place a stop-loss order at a price that guarantees a profit of `lock_profit_pips`. This stop-loss is also calculated relative to the order's entry grid line.

### Example

Let's assume the following for a `BUY_ONLY` strategy:
-   `grid_step_pips` = 10
-   `lock_trigger_pips` = 20
-   `lock_profit_pips` = 10

1.  A BUY order is opened at a grid line of 1.1000.
2.  The market price moves up.
3.  When the Bid price reaches 1.1020 (1.1000 + 20 pips), the profit has reached `lock_trigger_pips`.
4.  The EA is triggered to lock in profits. It places a stop-loss at 1.1010 (1.1000 + 10 pips).

This ensures that even if the price reverses after hitting 1.1020, the trade will be closed with a guaranteed profit of at least 10 pips.

## Disclaimer

Trading foreign exchange on margin carries a high level of risk and may not be suitable for all investors. The high degree of leverage can work against you as well as for you. Before deciding to trade foreign exchange, you should carefully consider your investment objectives, level of experience, and risk appetite. The possibility exists that you could sustain a loss of some or all of your initial investment and therefore you should not invest money that you cannot afford to lose. You should be aware of all the risks associated with foreign exchange trading and seek advice from an independent financial advisor if you have any doubts.
