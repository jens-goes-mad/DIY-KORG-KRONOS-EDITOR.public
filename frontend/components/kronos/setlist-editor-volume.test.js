// Headless, node-runnable version of setlist-editor-volume.test.html's
// self-checks -- same assertions, same real fixture as setlist-editor-
// comment-and-font.test.js, no browser/DOM needed since every function
// here is pure. Run directly: `node setlist-editor-volume.test.js`
// (requires ../package.json's `"type": "module"` to load as ESM).
//
// Exits non-zero on any failed assertion, for CI/ctest-style usage.

import { RECORD_SIZE, decodeSlotVolume, encodeSlotVolume } from "./setlist-editor-volume.js";
import { ROLLING_IN_THE_DEEP_RECORD_HEX, hexToBytes } from "./test-fixtures.js";

let failures = 0;

function check(label, actual, expected) {
  const pass = JSON.stringify(actual) === JSON.stringify(expected);
  if (!pass) {
    failures++;
    console.error(`FAIL -- ${label} (got ${JSON.stringify(actual)}, expected ${JSON.stringify(expected)})`);
  } else {
    console.log(`PASS -- ${label}`);
  }
}

const realRecord = hexToBytes(ROLLING_IN_THE_DEEP_RECORD_HEX);
check("record is the documented SBK1 stride", realRecord.length, RECORD_SIZE);

// Ground truth for this real record: byte+16 = 0x7f (Volume 127) -- confirmed
// against the same bytes src/kronos/PcgFile.cpp's readSlotParams() decodes.
check("decodeSlotVolume matches the real record's confirmed Volume (127)", decodeSlotVolume(realRecord), 127);

for (const volume of [0, 64, 127]) {
  const withVolume = encodeSlotVolume(realRecord, volume);
  check(`setting volume=${volume} round-trips through decode()`, decodeSlotVolume(withVolume), volume);
}
check("encodeSlotVolume clamps below 0 up to 0", decodeSlotVolume(encodeSlotVolume(realRecord, -5)), 0);
check("encodeSlotVolume clamps above 127 down to 127", decodeSlotVolume(encodeSlotVolume(realRecord, 200)), 127);

const volumeEdited = encodeSlotVolume(realRecord, 42);
check(
  "bytes other than +16 are untouched by a Volume edit",
  Array.from(volumeEdited.slice(0, 16)).concat(Array.from(volumeEdited.slice(17))),
  Array.from(realRecord.slice(0, 16)).concat(Array.from(realRecord.slice(17)))
);

let threw = false;
try {
  decodeSlotVolume(new Uint8Array(10));
} catch {
  threw = true;
}
check("decodeSlotVolume rejects a byte array of the wrong length", threw, true);

if (failures > 0) {
  console.error(`\n${failures} check(s) FAILED`);
  process.exit(1);
} else {
  console.log("\nAll checks passed");
}
