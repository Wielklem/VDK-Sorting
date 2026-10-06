#pragma once

#include <QString>
#include <QUrl>
#include <QtPlugin>

namespace vsort::hmi {

// One GUI page (G20..G130). Implemented by page plugins, loaded at startup.
class IPage {
public:
    IPage() = default;
    IPage(const IPage&) = delete;
    IPage& operator=(const IPage&) = delete;
    IPage(IPage&&) = delete;
    IPage& operator=(IPage&&) = delete;
    virtual ~IPage() = default;

    [[nodiscard]] virtual QString pageId() const = 0; // "G30"
    [[nodiscard]] virtual QString title() const = 0;  // "Cameras"
    [[nodiscard]] virtual int order() const = 0;      // navigation order
    [[nodiscard]] virtual QUrl qmlSource() const = 0; // root QML of the page
};

} // namespace vsort::hmi

Q_DECLARE_INTERFACE(vsort::hmi::IPage, "vdk.vsort.IPage/1")
