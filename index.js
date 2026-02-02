const path = require('path');
const fs = require('fs');

function resolveBinding() {
  const releasePath = path.join(__dirname, 'build', 'Release', 'inputhook.node');
  const debugPath = path.join(__dirname, 'build', 'Debug', 'inputhook.node');

  if (fs.existsSync(releasePath)) {
    return releasePath;
  }
  if (fs.existsSync(debugPath)) {
    return debugPath;
  }

  throw new Error('inputhook native addon not built. Run `npm run build` first.');
}

const binding = require(resolveBinding());

module.exports = {
  start: binding.start,
  stop: binding.stop,
  onEvent: binding.onEvent
};
