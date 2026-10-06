#include <algorithm>
#include <cstddef>

#include <vsort/hmi/page_registry.hpp>

namespace vsort::hmi {

PageRegistry::PageRegistry(QObject* parent)
    : QAbstractListModel(parent) {}

bool PageRegistry::addPage(IPage* page) {
    if (page == nullptr) {
        return false;
    }
    const QString id = page->pageId();
    const bool duplicate =
        std::ranges::any_of(pages_, [&id](const IPage* p) { return p->pageId() == id; });
    if (duplicate) {
        return false;
    }
    const auto pos = std::ranges::upper_bound(pages_, page->order(), {}, &IPage::order);
    const int row = static_cast<int>(pos - pages_.begin());
    beginInsertRows({}, row, row);
    pages_.insert(pos, page);
    endInsertRows();
    emit countChanged();
    return true;
}

int PageRegistry::count() const {
    return static_cast<int>(pages_.size());
}

QUrl PageRegistry::sourceAt(int index) const {
    if (index < 0 || index >= count()) {
        return {};
    }
    return pages_[static_cast<std::size_t>(index)]->qmlSource();
}

int PageRegistry::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant PageRegistry::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const IPage* page = pages_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case PageIdRole:
        return page->pageId();
    case TitleRole:
        return page->title();
    case SourceRole:
        return page->qmlSource();
    default:
        return {};
    }
}

QHash<int, QByteArray> PageRegistry::roleNames() const {
    return {{PageIdRole, "pageId"}, {TitleRole, "title"}, {SourceRole, "source"}};
}

} // namespace vsort::hmi
