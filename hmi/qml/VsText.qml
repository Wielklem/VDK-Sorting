import QtQuick

Text {
    enum Variant {
        Body,
        Caption,
        Title,
        Heading
    }

    property int variant: VsText.Body

    color: variant === VsText.Caption ? Theme.textSecondary : Theme.textPrimary
    font.family: Theme.fontFamily
    font.pixelSize: {
        switch (variant) {
        case VsText.Caption:
            return Theme.sizeCaption;
        case VsText.Title:
            return Theme.sizeTitle;
        case VsText.Heading:
            return Theme.sizeHeading;
        default:
            return Theme.sizeBody;
        }
    }
    font.weight: (variant === VsText.Title || variant === VsText.Heading) ? Font.DemiBold : Font.Normal
    wrapMode: Text.WordWrap
}
