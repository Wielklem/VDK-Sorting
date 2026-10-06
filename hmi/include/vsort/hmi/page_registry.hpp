#pragma once

#include <QAbstractListModel>
#include <QUrl>
#include <vector>

#include <vsort/hmi/ipage.hpp>

namespace vsort::hmi {

// Navigation model: pages sorted by order(), unique pageId. Does not own the pages.
class PageRegistry : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { PageIdRole = Qt::UserRole + 1, TitleRole, SourceRole };

    explicit PageRegistry(QObject* parent = nullptr);

    // False if page is null or its id is already registered.
    bool addPage(IPage* page);
    [[nodiscard]] int count() const;
    Q_INVOKABLE [[nodiscard]] QUrl sourceAt(int index) const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    std::vector<IPage*> pages_;
};

} // namespace vsort::hmi
