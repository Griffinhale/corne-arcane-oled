const HOST = 'io.github.griffinhale.corne_arcane';
let port = null;

function send(kind, intensity) {
    if (!['scroll', 'tab', 'page'].includes(kind) || !Number.isInteger(intensity) ||
        intensity < 0 || intensity > 3)
        return;
    try {
        if (!port)
            port = browser.runtime.connectNative(HOST);
        port.postMessage({kind, intensity});
    } catch (_) {
        port = null; // Missing permission/host disables only this adapter.
    }
}

browser.runtime.onMessage.addListener(message => {
    if (message && Object.keys(message).length === 2)
        send(message.kind, message.intensity);
});
browser.tabs.onActivated.addListener(() => send('tab', 1));
browser.webNavigation.onCompleted.addListener(() => send('page', 1));
