/**
 * Self-test for rollback stack policy and apply (no HTTP). Run:
 *   node status-server/tests/rollbackApi.selftest.js
 */
const fs = require('fs');
const path = require('path');
const os = require('os');
const assert = require('assert');
const {
  pushEntryToFeatureState,
  buildRollbackStackEntry,
  MAX_ROLLBACK_STACK_SIZE,
} = require('../services/rollbackStackSync');
const {
  applyRollback,
  previewRollback,
  verifyRollback,
} = require('../services/rollbackApi');

function tmpDir() {
  const d = fs.mkdtempSync(path.join(os.tmpdir(), 'rollback-test-'));
  fs.mkdirSync(path.join(d, 'state'), { recursive: true });
  fs.mkdirSync(path.join(d, 'history'), { recursive: true });
  return d;
}

function writeCar(deployRoot, carId, features) {
  const fp = path.join(deployRoot, 'state', `${carId}.json`);
  fs.writeFileSync(
    fp,
    JSON.stringify(
      {
        car_id: carId,
        features,
      },
      null,
      2,
    ),
  );
  return fp;
}

// Test 1: stack max 3
{
  const f = { rollback_stack: [] };
  for (let i = 1; i <= 4; i += 1) {
    pushEntryToFeatureState(
      f,
      buildRollbackStackEntry({
        version: `v${i}.0`,
        feature: 'ADAS',
        replaced_by: `v${i + 1}.0`,
      }),
    );
  }
  assert.strictEqual(f.rollback_stack.length, MAX_ROLLBACK_STACK_SIZE);
  assert.strictEqual(f.rollback_stack[0].version, 'v4.0');
  assert.strictEqual(f.rollback_stack[2].version, 'v2.0');
  console.log('OK test1 stack limit');
}

// Test 2: apply rollback changes primary
{
  const deployRoot = tmpDir();
  const rtDir = path.join(deployRoot, 'vehicle_runtime');
  fs.mkdirSync(rtDir, { recursive: true });
  writeCar(deployRoot, 'car_1', {
    ADAS: {
      primary_version: 'v3.0',
      active_version: 'v3.0',
      primary_slot: 'B',
      active_slot: 'B',
      primary_image: 'localhost:5000/sdv/adas-feature:v3.0',
      rollback_stack: [
        buildRollbackStackEntry({
          version: 'v2.0',
          docker_image: 'localhost:5000/sdv/adas-feature:v2.0',
          slot: 'B',
          feature: 'ADAS',
          replaced_by: 'v3.0',
        }),
        buildRollbackStackEntry({ version: 'v1.0', feature: 'ADAS' }),
      ],
    },
    Navigation: { primary_version: 'v9.0', rollback_stack: [] },
    Energy: { primary_version: 'v9.0', rollback_stack: [] },
  });
  fs.writeFileSync(
    path.join(rtDir, 'car_1.json'),
    JSON.stringify({
      car_id: 'car_1',
      domains: {
        adas: { primary_version: 'v3.0' },
        navigation: { primary_version: 'v9.0' },
        evenergy: { primary_version: 'v9.0' },
      },
    }),
  );

  const res = applyRollback({
    deployRoot,
    vehicleRuntimeDir: rtDir,
    carId: 'car_1',
    feature: 'ADAS',
    targetVersion: 'v2.0',
    syncConfig: false,
  });
  assert.strictEqual(res.ok, true);
  assert.strictEqual(res.restored_version, 'v2.0');
  const after = JSON.parse(
    fs.readFileSync(path.join(deployRoot, 'state', 'car_1.json'), 'utf8'),
  );
  assert.strictEqual(after.features.ADAS.primary_version, 'v2.0');
  assert.strictEqual(after.features.Navigation.primary_version, 'v9.0');
  const rt = JSON.parse(fs.readFileSync(path.join(rtDir, 'car_1.json'), 'utf8'));
  assert.strictEqual(rt.domains.adas.primary_version, 'v2.0');
  console.log('OK test2 apply state + runtime');

  const verify = verifyRollback({
    deployRoot,
    vehicleRuntimeDir: rtDir,
    carId: 'car_1',
    feature: 'ADAS',
    expectedPrimary: 'v2.0',
  });
  assert.strictEqual(verify.verified, true);
  console.log('OK test4 verify');

  const prev = previewRollback({
    deployRoot,
    carId: 'car_1',
    feature: 'ADAS',
  });
  assert.strictEqual(prev.rollback_available, true);
  console.log('OK preview after rollback');
}

// Test 5: empty stack fails without state change
{
  const deployRoot = tmpDir();
  const rtDir = path.join(deployRoot, 'vehicle_runtime');
  fs.mkdirSync(rtDir, { recursive: true });
  writeCar(deployRoot, 'car_2', {
    ADAS: {
      primary_version: 'v1.0',
      rollback_stack: [],
    },
  });
  const res = applyRollback({
    deployRoot,
    vehicleRuntimeDir: rtDir,
    carId: 'car_2',
    feature: 'ADAS',
  });
  assert.strictEqual(res.ok, false);
  const after = JSON.parse(
    fs.readFileSync(path.join(deployRoot, 'state', 'car_2.json'), 'utf8'),
  );
  assert.strictEqual(after.features.ADAS.primary_version, 'v1.0');
  console.log('OK test5 empty stack');
}

console.log('All rollback self-tests passed.');
