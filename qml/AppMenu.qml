// Every menu of the app. It opens in a window of its own, so a menu taller
// than a small patch window is not pushed up over the menu bar, and a
// submenu's icon gets the same 16 px as the items, keeping rows one height.
import QtQuick
import QtQuick.Controls

Menu {
    property string iconName

    popupType: Popup.Window
    icon.source: iconName ? Qt.resolvedUrl(`icons/${iconName}.svg`) : ""
    icon.color: palette.windowText
    icon.width: 16
    icon.height: 16
}
