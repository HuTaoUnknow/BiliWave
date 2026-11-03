#include "window.h"
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMessageBox>

QPushButton *MainWindow::collectionButton(QListWidgetItem *item, int action) const {
    const auto cell = item ? m_jobs->itemWidget(item) : nullptr;
    const QStringList names{"discardDownloadedTrack", "collectDownloadedTrack", "openDownloadedTrack"};
    return cell ? cell->findChild<QPushButton *>(names.value(action)) : nullptr;
}
void MainWindow::focusCollectionItem(int row, int action) {
    m_collectionAction = qBound(0, action, 2);
    if (!m_jobs->count()) {
        m_jobs->setFocus(Qt::TabFocusReason);
        return;
    }
    row = qBound(0, row, m_jobs->count() - 1);
    m_jobs->setCurrentRow(row);
    const auto item = m_jobs->item(row);
    m_jobs->scrollToItem(item);
    auto button = collectionButton(item, m_collectionAction);
    if (!button || !button->isEnabled()) {
        m_collectionAction = 0;
        button = collectionButton(item, 0);
    }
    if (button) button->setFocus(Qt::TabFocusReason);
    else m_jobs->setFocus(Qt::TabFocusReason);
}
void MainWindow::chooseSearchResult(QTreeWidgetItem *item) {
    if (!item || m_searchChoiceActive) return;
    m_searchChoiceActive = true;
    QMessageBox choice(this);
    choice.setObjectName("searchResultAction");
    choice.setWindowTitle("收录歌曲");
    choice.setText(item->text(1));
    auto url = choice.addButton("URL 收录", QMessageBox::AcceptRole);
    auto download = choice.addButton("下载", QMessageBox::ActionRole);
    auto cancel = choice.addButton("取消", QMessageBox::RejectRole);
    for (auto button : {url, download, cancel}) button->setProperty("searchChoice", true);
    choice.setDefaultButton(url);
    choice.setEscapeButton(cancel);
    QWidget::setTabOrder(url, download);
    QWidget::setTabOrder(download, cancel);
    QWidget::setTabOrder(cancel, url);
    url->setFocus();
    choice.exec();
    if (choice.clickedButton() == url) collectUrls();
    else if (choice.clickedButton() == download) downloadResults();
    m_searchChoiceActive = false;
    m_results->setFocus(Qt::OtherFocusReason);
    // Returning from this dialog is the exception to the normal window-activation focus.
    QTimer::singleShot(0, this, [this] {
        if (m_pages->currentIndex() == 0) m_results->setFocus(Qt::OtherFocusReason);
    });
}
bool MainWindow::handleInteractionEvent(QObject *watched, QEvent *event) {
    if (watched == this && event->type() == QEvent::WindowActivate && !m_searchChoiceActive) {
        QTimer::singleShot(0, this, [this] {
            if (isActiveWindow() && !QApplication::activeModalWidget() && !m_searchChoiceActive)
                m_toggle->setFocus(Qt::ActiveWindowFocusReason);
        });
    }
    auto widget = qobject_cast<QWidget *>(watched);
    if (!widget || widget->window() != this || QApplication::activeModalWidget()) return false;
    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove ||
        event->type() == QEvent::Drop) {
        auto drop = static_cast<QDropEvent *>(event);
        const auto mime = drop->mimeData();
        if (!mime->hasUrls() || mime->hasFormat(TrackMime) || mime->hasFormat(PlaylistMime)) return false;
        QStringList paths;
        for (const auto &url : mime->urls())
            if (url.isLocalFile() && (QFileInfo(url.toLocalFile()).isDir() ||
                                      ImportService::acceptsLocalFile(url.toLocalFile())))
                paths << url.toLocalFile();
        if (paths.isEmpty()) return false;
        if (event->type() == QEvent::Drop) m_import.enqueueLocalFiles(paths);
        drop->setDropAction(Qt::CopyAction);
        drop->accept();
        return true;
    }
    const bool searchPage = m_pages->currentIndex() == 0;
    const bool inJobs = widget == m_jobs || m_jobs->isAncestorOf(widget);
    if (searchPage && inJobs && event->type() == QEvent::FocusIn) {
        if (auto button = qobject_cast<QPushButton *>(widget); button && button->property("downloadAction").isValid()) {
            m_collectionAction = button->property("downloadAction").toInt();
            for (int row = 0; row < m_jobs->count(); ++row)
                if (collectionButton(m_jobs->item(row), m_collectionAction) == button) {
                    m_jobs->setCurrentRow(row);
                    break;
                }
        }
    }
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride) return false;
    auto key = static_cast<QKeyEvent *>(event);
    const auto modifiers = key->modifiers() & ~Qt::KeypadModifier;
    const bool shortcut = event->type() == QEvent::ShortcutOverride;
    if (searchPage && modifiers == Qt::ControlModifier && key->key() == Qt::Key_F) {
        key->accept();
        if (!shortcut) { m_focusResultsAfterSearch = false; m_input->setFocus(); m_input->selectAll(); }
        return true;
    }
    if (modifiers != Qt::NoModifier) return false;
    const int code = key->key();
    const bool enter = code == Qt::Key_Return || code == Qt::Key_Enter;
    const bool arrow = code == Qt::Key_Up || code == Qt::Key_Down || code == Qt::Key_Left || code == Qt::Key_Right;
    const bool results = widget == m_results || m_results->isAncestorOf(widget);
    const bool transport = widget == m_toggle;
    const bool input = widget == m_input;
    if (!((transport && (enter || arrow || (searchPage && code == Qt::Key_Tab))) ||
          (searchPage && (results || inJobs) && (enter || arrow || code == Qt::Key_Tab)) ||
          (searchPage && input && code == Qt::Key_Tab))) return false;
    key->accept();
    if (shortcut || (enter && key->isAutoRepeat())) return true;
    if (transport) {
        if (enter) m_toggle->click();
        else if (code == Qt::Key_Left) m_player.previous();
        else if (code == Qt::Key_Right) m_player.next();
        else if (code == Qt::Key_Tab) { m_input->setFocus(Qt::TabFocusReason); m_input->selectAll(); }
        else {
            int volume = qRound(m_player.volume() * 100);
            if (code == Qt::Key_Up)
                volume = volume < 5 ? qMin(5, volume + 1) : volume < 20 ? qMin(20, volume + 5) : qMin(100, volume + 10);
            else
                volume = volume > 20 ? qMax(20, volume - 10) : volume > 5 ? qMax(5, volume - 5) : qMax(0, volume - 1);
            findChild<QSlider *>("playerVolume")->setValue(volume);
        }
    } else if (results) {
        if (code == Qt::Key_Tab) { m_focusResultsAfterSearch = false; focusCollectionItem(qMax(0, m_jobs->currentRow()), 0); }
        else if (enter) chooseSearchResult(m_results->currentItem());
        else if (code == Qt::Key_Up || code == Qt::Key_Down) {
            const int row = m_results->indexOfTopLevelItem(m_results->currentItem());
            const int next = qBound(0, row + (code == Qt::Key_Up ? -1 : 1), qMax(0, m_results->topLevelItemCount() - 1));
            if (auto item = m_results->topLevelItem(next)) {
                m_results->setCurrentItem(item, 0, QItemSelectionModel::ClearAndSelect);
                m_results->scrollToItem(item);
            }
        } else {
            auto button = code == Qt::Key_Left ? m_previousPage : m_nextPage;
            if (button->isEnabled()) { m_focusResultsAfterSearch = true; button->click(); }
        }
    } else if (inJobs) {
        if (code == Qt::Key_Tab) m_toggle->setFocus(Qt::TabFocusReason);
        else if (enter) {
            const int row = m_jobs->currentRow();
            if (auto button = collectionButton(m_jobs->currentItem(), m_collectionAction); button && button->isEnabled()) {
                button->click();
                if (m_pages->currentIndex() == 0) focusCollectionItem(row, m_collectionAction);
            }
        } else if (code == Qt::Key_Up || code == Qt::Key_Down)
            focusCollectionItem(m_jobs->currentRow() + (code == Qt::Key_Up ? -1 : 1), m_collectionAction);
        else {
            int action = m_collectionAction;
            const int step = code == Qt::Key_Left ? -1 : 1;
            while ((action += step) >= 0 && action <= 2)
                if (auto button = collectionButton(m_jobs->currentItem(), action); button && button->isEnabled()) {
                    focusCollectionItem(m_jobs->currentRow(), action);
                    break;
                }
        }
    } else if (input) {
        if (m_results->topLevelItemCount()) m_results->setFocus(Qt::TabFocusReason);
        else focusCollectionItem(qMax(0, m_jobs->currentRow()), 0);
    }
    return true;
}
