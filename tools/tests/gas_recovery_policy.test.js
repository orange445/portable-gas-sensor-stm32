"use strict";

const assert = require("node:assert/strict");
const policy = require("../gas_recovery_policy.js");

const firstConnection = policy.assessInfo("POWER_BOR", 13900, null);
assert.equal(firstConnection.powerReset, true);
assert.equal(firstConnection.restarted, false);
assert.equal(firstConnection.guardMs, 1100);

const afterUnexpectedReset = policy.assessInfo("POWER_BOR", 3700, 13900);
assert.equal(afterUnexpectedReset.restarted, true);
assert.equal(afterUnexpectedReset.guardMs, 11300);

const stableBoard = policy.assessInfo("POWER_BOR", 18000, 3700);
assert.equal(stableBoard.restarted, false);
assert.equal(stableBoard.guardMs, 0);

const pinReset = policy.assessInfo("NRST", 2000, 18000);
assert.equal(pinReset.restarted, true);
assert.equal(pinReset.guardMs, 0);

const delayedReconnectAfterReset = policy.assessInfo(
  "POWER_BOR", 20000, 13900, 30000
);
assert.equal(delayedReconnectAfterReset.restarted, true);

const delayedReconnectWithoutReset = policy.assessInfo(
  "POWER_BOR", 44000, 13900, 30000
);
assert.equal(delayedReconnectWithoutReset.restarted, false);

assert.deepEqual(
  [0, 1, 2, 3, 8].map(policy.reconnectDelay),
  [2500, 5000, 10000, 20000, 20000]
);

console.log("gas recovery policy replay: PASS");
