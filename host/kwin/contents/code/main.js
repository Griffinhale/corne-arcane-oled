// KWin 6 privacy boundary: report application identifiers only. Never read or
// transmit caption/title, URL, tab, document, or page content.
const service = "io.github.Griffinhale.CorneArcane";
const path = "/io/github/Griffinhale/CorneArcane";
const iface = "io.github.Griffinhale.CorneArcane.Focus";

function reportWindow(window) {
    const resourceClass = window ? String(window.resourceClass || "") : "";
    const desktopFileName = window ? String(window.desktopFileName || "") : "";
    callDBus(service, path, iface, "ReportActiveWindow", resourceClass, desktopFileName);
}

workspace.windowActivated.connect(reportWindow);
reportWindow(workspace.activeWindow);
