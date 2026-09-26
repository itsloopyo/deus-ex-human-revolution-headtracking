// Runs core's canonical config lint over the committed config and over every
// distinct CameraUnlock.ini the differential test migrated (the folder named as
// the argument).
//
// A migrated file holds the player's own value on each row they set away from its
// default, and the lint reports that as a committed file that does not hold
// `default` there. That rule is the committed file's, so it is the one problem a
// migrated file may have.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { lintCanonicalConfig } from "../../cameraunlock-core/scripts/check-canonical-config.mjs";

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const migratedDir = process.argv[2];
if (!migratedDir) throw new Error("usage: node lint-migrated.mjs <folder of migrated files>");

const REPO = "deus-ex-human-revolution-headtracking";
const format = JSON.parse(fs.readFileSync(path.join(repo, "cameraunlock-core", "data", "config-format.json"), "utf8"));
const options = {
  dialect: format.configs[REPO][0].dialect,
  perGame: (format.per_game[REPO] ?? []).map((entry) => entry.row),
};
const PLAYER_VALUE = /a committed file holds default on every global concept row/;
const failures = [];

const committed = "DeusExHumanRevolutionHeadTracking.ini";
for (const problem of lintCanonicalConfig(fs.readFileSync(path.join(repo, committed)), options)) {
  failures.push(`${committed}: ${problem}`);
}

const files = fs.readdirSync(migratedDir).filter((f) => f.endsWith(".ini"));
if (files.length === 0) throw new Error(`${migratedDir} holds no migrated files`);
for (const file of files) {
  for (const problem of lintCanonicalConfig(fs.readFileSync(path.join(migratedDir, file)), options)) {
    if (!PLAYER_VALUE.test(problem)) failures.push(`${file}: ${problem}`);
  }
}

if (failures.length > 0) {
  for (const f of failures) console.log(`FAIL ${f}`);
  process.exit(1);
}
console.log(`canonical config lint: the committed file and ${files.length} migrated files pass`);
