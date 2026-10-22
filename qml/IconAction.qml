// An Action with a Lucide icon (https://lucide.dev, ISC license) from
// qml/icons. The icons are drawn white and tinted here like menu text, so
// they follow the light or dark palette.
import QtQuick
import QtQuick.Controls

Action {
    id: action
    property string iconName

    readonly property SystemPalette active: SystemPalette {}
    readonly property SystemPalette disabled: SystemPalette { colorGroup: SystemPalette.Disabled }

    icon.source: iconName ? Qt.resolvedUrl(`icons/${iconName}.svg`) : ""
    icon.color: enabled ? active.windowText : disabled.windowText
}
