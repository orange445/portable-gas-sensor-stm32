"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const firmwarePath = path.resolve(
  __dirname,
  "../../firmware/cubemx/Core/Src/gas_sensor.c"
);
const firmware = fs.readFileSync(firmwarePath, "utf8");
const plotterPath = path.resolve(__dirname, "../serial_curve_plotter.html");
const plotter = fs.readFileSync(plotterPath, "utf8");
const helloStart = firmware.indexOf('strcmp(command, "HELLO")');
const helloEnd = firmware.indexOf("if (strcmp(command, \"GAS,START\")", helloStart);
const helloBlock = firmware.slice(helloStart, helloEnd);

assert.ok(helloStart >= 0 && helloEnd > helloStart, "HELLO command block missing");
assert.doesNotMatch(helloBlock, /GAS,ACK,HELLO/);
assert.ok(
  helloBlock.indexOf("Gas_SendInfo()") < helloBlock.indexOf("Gas_SendStatus()"),
  "legacy 0.2.1 HELLO must emit INFO followed by STATUS"
);
assert.match(firmware, /#define GAS_FW_VERSION\s+"0\.2\.1"/);
assert.doesNotMatch(firmware, /start_delay_ms=/);
assert.doesNotMatch(firmware, /usb_safe_hz=/);
assert.doesNotMatch(firmware, /GasSensor_EarlyCommInit/);
assert.doesNotMatch(firmware, /GAS,ALIVE/);
assert.match(firmware, /GAS_DEFAULT_OUTPUT_RATE_HZ\s+10U/);
assert.match(firmware, /GAS_WARMUP_TIME_MS\s+150UL/);
assert.match(firmware, /Gas_SetAnalogPower\(1U\)/);
assert.match(firmware, /Gas_SetState\(GAS_STATE_WARMUP, "START"\)/);

const bleSendStart = plotter.indexOf("async function sendBluetoothLine");
const bleSendEnd = plotter.indexOf("async function sendDeviceLine", bleSendStart);
const bleSendBlock = plotter.slice(bleSendStart, bleSendEnd);
assert.ok(bleSendStart >= 0 && bleSendEnd > bleSendStart, "BLE send block missing");
assert.ok(
  bleSendBlock.indexOf("writeValueWithoutResponse") <
    bleSendBlock.indexOf("writeValue(chunk)"),
  "VG7439 FFE1 must prefer Write Without Response"
);
assert.doesNotMatch(bleSendBlock, /writeValueWithResponse/);
assert.match(plotter, /gasAfeReady = values\.afe_ready === undefined \? true/);
assert.match(plotter, /!gasHandshakeComplete \|\| !gasAfeReady/);
assert.match(plotter, /type === "FAULT"/);
assert.doesNotMatch(plotter, /type === "ALIVE"/);

console.log("gas firmware and BLE transport contract: PASS");
