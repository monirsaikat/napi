const inputhook = require('.');

const durationMs = 600000;
let stopTimer;

function stopHook(source) {
  if (stopTimer) {
    clearTimeout(stopTimer);
    stopTimer = null;
  }
  inputhook.stop();
  console.log(`inputhook stopped (${source}).`);
}

inputhook.onEvent((event) => {
  console.log('event', JSON.stringify(event));
});

const started = inputhook.start();
if (!started) {
  console.error('inputhook failed to start; ensure the native addon is built.');
  process.exitCode = 1;
} else {
  console.log('inputhook is listening for global input events for 10 seconds...');
  console.log('Move the mouse or press a key to see events. Press Ctrl+C to abort early.');

  stopTimer = setTimeout(() => {
    stopHook('timeout');
    process.exit(0);
  }, durationMs);

  const handleSignal = (signal) => {
    console.log(`received ${signal}`);
    stopHook(signal);
    process.exit(0);
  };

  process.once('SIGINT', () => handleSignal('SIGINT'));
  process.once('SIGTERM', () => handleSignal('SIGTERM'));
}
