let last = -Infinity;
let burst = 0;

addEventListener('scroll', () => {
    const now = performance.now();
    burst = Math.min(3, burst + 1);
    if (now - last < 250) {
        return;
    }
    browser.runtime.sendMessage({kind: 'scroll', intensity: Math.max(1, burst)});
    burst = 0;
    last = now;
}, {passive: true});
