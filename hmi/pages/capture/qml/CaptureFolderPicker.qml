import QtQuick
import QtQuick.Dialogs

// Folder dialog of the Capture tab. Loaded on demand by CapturePage, so a missing Qt Quick Dialogs
// module only disables the Browse button (the folder can still be typed).
Item {
    id: root

    signal picked(string path)

    function open() {
        dialog.open();
    }

    // file:///home/x -> /home/x, file:///C:/x -> C:/x
    function localPath(url) {
        const text = decodeURIComponent(url.toString());
        if (/^file:\/\/\/[A-Za-z]:/.test(text))
            return text.substring(8);
        return text.replace(/^file:\/\//, "");
    }

    FolderDialog {
        id: dialog

        title: "Select the folder for the photos"
        onAccepted: root.picked(root.localPath(dialog.selectedFolder))
    }
}
