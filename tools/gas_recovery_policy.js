(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) {
    module.exports = api;
  } else {
    root.GasRecoveryPolicy = api;
  }
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";

  const POWER_STABLE_UPTIME_MS = 15000;
  const RECONNECT_DELAYS_MS = Object.freeze([2500, 5000, 10000, 20000]);

  function finiteNumber(value) {
    const number = Number(value);
    return Number.isFinite(number) ? number : null;
  }

  function assessInfo(resetReason, uptimeValue, previousUptimeValue,
                      elapsedSincePreviousInfoValue) {
    const uptimeMs = finiteNumber(uptimeValue);
    const previousUptimeMs = finiteNumber(previousUptimeValue);
    const elapsedSincePreviousInfoMs = Math.max(
      0,
      finiteNumber(elapsedSincePreviousInfoValue) || 0
    );
    const powerReset = resetReason === "POWER_BOR";
    const restarted = uptimeMs !== null && previousUptimeMs !== null &&
      uptimeMs + 1000 < previousUptimeMs + elapsedSincePreviousInfoMs;
    const guardMs = powerReset && uptimeMs !== null
      ? Math.max(0, POWER_STABLE_UPTIME_MS - uptimeMs)
      : 0;

    return Object.freeze({ powerReset, restarted, uptimeMs, guardMs });
  }

  function reconnectDelay(attemptIndex) {
    const index = Math.max(0, Math.floor(Number(attemptIndex) || 0));
    return RECONNECT_DELAYS_MS[Math.min(index, RECONNECT_DELAYS_MS.length - 1)];
  }

  return Object.freeze({
    POWER_STABLE_UPTIME_MS,
    RECONNECT_DELAYS_MS,
    assessInfo,
    reconnectDelay,
  });
});
