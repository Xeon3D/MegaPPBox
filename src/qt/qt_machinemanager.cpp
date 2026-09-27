/*
 * MegaPPBox - Merit Megatouch cabinets on 86Box.
 *
 *          The Machine Manager.  It scans a folder (and its subfolders) of
 *          disk and CD images, reads out of each what it is, and lists the
 *          ones this emulator can run.  Picking one sets the hardware profile
 *          and the security key that release wants; both can be changed.
 *
 *          Nothing is kept but the folder (in MegaPPBox.cfg, with the image
 *          in use and its profile, board and key): the folder is read again
 *          each time the manager opens, as in PeepeeBox.
 *
 * Authors: MegaPPBox contributors
 *
 *          Released under the GNU General Public License version 2 or
 *          later.  See COPYING for more information.
 */
#include "qt_machinemanager.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/megatouch.h>
}

namespace {

enum Column { ColRelease, ColVersion, ColProfile, ColFile, ColCount };

const QStringList imageFilters = { "*.img", "*.ima", "*.hdd", "*.raw", "*.iso" };

QString
profileName(int p)
{
    return ((p >= 0) && (p < MT_PROFILE_COUNT)) ? QString::fromLatin1(mt_profiles[p].name) : QString();
}

} // namespace

MachineManager::MachineManager(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Machine Manager"));
    setWindowIcon(QIcon(":/menuicons/qt/icons/machine_manager.ico"));
    resize(900, 560);

    auto *top = new QHBoxLayout;
    folder    = new QLineEdit(QString::fromUtf8(megatouch_library()));
    folder->setPlaceholderText(tr("Folder with Megatouch disk (.img) and CD (.iso) images"));
    auto *browseBtn = new QPushButton(tr("&Browse…"));
    auto *scanBtn   = new QPushButton(tr("&Scan"));
    top->addWidget(new QLabel(tr("Images:")));
    top->addWidget(folder, 1);
    top->addWidget(browseBtn);
    top->addWidget(scanBtn);

    tree = new QTreeWidget;
    tree->setColumnCount(ColCount);
    tree->setHeaderLabels({ tr("Release"), tr("Version"), tr("Profile"), tr("File") });
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    tree->setSortingEnabled(true);
    tree->header()->setSectionResizeMode(ColFile, QHeaderView::Stretch);

    showAll = new QCheckBox(tr("Show images that cannot run here"));
    status  = new QLabel;

    profile = new QComboBox;
    for (int i = 0; i < MT_PROFILE_COUNT; i++)
        profile->addItem(QString("%1 — %2").arg(profileName(i), QString::fromLatin1(mt_profiles[i].description)), i);
    board = new QComboBox;
    board->addItem(tr("The profile's own (ASUS TX97, i430TX)"), MT_BOARD_DEFAULT);
    board->addItem(tr("ASUS P/I-P55TVP4 (i430VX)"), MT_BOARD_P55TVP4);
    key     = new QComboBox;
    details = new QLabel;
    details->setWordWrap(true);
    details->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *form = new QFormLayout;
    form->addRow(tr("Hardware profile:"), profile);
    form->addRow(tr("Motherboard:"), board);
    form->addRow(tr("Key:"), key);
    form->addRow(details);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    run           = buttons->addButton(tr("&Run"), QDialogButtonBox::AcceptRole);
    run->setEnabled(false);

    auto *below = new QHBoxLayout;
    below->addWidget(showAll);
    below->addStretch(1);
    below->addWidget(status);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(tree, 1);
    layout->addLayout(below);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(browseBtn, &QPushButton::clicked, this, &MachineManager::browse);
    connect(scanBtn, &QPushButton::clicked, this, &MachineManager::scan);
    connect(showAll, &QCheckBox::toggled, this, &MachineManager::populate);
    connect(tree, &QTreeWidget::itemSelectionChanged, this, &MachineManager::selectionChanged);
    connect(tree, &QTreeWidget::itemDoubleClicked, this, [this]() { if (run->isEnabled()) accept(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &MachineManager::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &MachineManager::reject);

    /* A changed profile / board / key is remembered with the image. */
    connect(profile, QOverload<int>::of(&QComboBox::activated), this, [this]() {
        if (Entry *e = current()) {
            e->userProfile = profile->currentData().toInt();
            board->setEnabled(MT_IS_MAXX(e->userProfile));
        }
    });
    connect(board, QOverload<int>::of(&QComboBox::activated), this, [this]() {
        if (Entry *e = current()) {
            e->userBoard = board->currentData().toInt();
        }
    });
    connect(key, QOverload<int>::of(&QComboBox::activated), this, [this]() {
        if (Entry *e = current()) {
            e->userKey = key->currentData().toString();
            e->keySet  = true;
        }
    });

    /* Earlier builds kept a scan cache next to the configuration. */
    QFile::remove(QDir(QString::fromUtf8(usr_path)).filePath("megappbox-library.ini"));

    if (!folder->text().isEmpty())
        scan();
    else
        populate();
}

QString
MachineManager::keysDir() const
{
    return QDir(QString::fromUtf8(usr_path)).filePath("keys");
}

void
MachineManager::browse()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder of Megatouch images"), folder->text());
    if (dir.isEmpty())
        return;
    folder->setText(QDir::toNativeSeparators(dir));
    scan();
}

void
MachineManager::scan()
{
    const QString dir = folder->text().trimmed();
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        status->setText(tr("Choose a folder first."));
        return;
    }
    megatouch_set_library(QDir::toNativeSeparators(dir).toUtf8().constData());

    QStringList files;
    QDirIterator it(dir, imageFilters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        files << it.next();
    files.sort(Qt::CaseInsensitive);

    QList<Entry> old = entries;
    entries.clear();

    QProgressDialog progress(tr("Reading images…"), tr("Stop"), 0, files.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);

    for (int i = 0; i < files.size(); i++) {
        progress.setValue(i);
        if (progress.wasCanceled())
            break;
        const QFileInfo fi(files[i]);
        Entry           e;
        e.path  = QDir::toNativeSeparators(fi.absoluteFilePath());
        e.size  = fi.size();
        e.mtime = fi.lastModified().toSecsSinceEpoch();

        bool cached = false;
        for (const Entry &o : old) {
            if ((o.path.compare(e.path, Qt::CaseInsensitive) == 0) && (o.size == e.size) && (o.mtime == e.mtime)) {
                e      = o;
                cached = true;
                break;
            }
        }
        if (!cached) {
            progress.setLabelText(tr("Reading %1…").arg(fi.fileName()));
            e.id = mt_identify(fi.absoluteFilePath());
        }
        entries.append(e);
    }
    progress.setValue(files.size());

    /* The image in use keeps the profile, board and key it runs with. */
    const QString cur = QString::fromUtf8(megatouch_image());
    for (Entry &e : entries) {
        if (!cur.isEmpty() && (e.path.compare(QDir::toNativeSeparators(cur), Qt::CaseInsensitive) == 0)) {
            e.userProfile = megatouch_profile();
            e.userBoard   = megatouch_board();
            e.keySet      = true;
            e.userKey     = QString::fromUtf8(megatouch_key());
        }
    }

    config_save();
    populate();
}

void
MachineManager::populate()
{
    const QString cur = QString::fromUtf8(megatouch_image());
    tree->setSortingEnabled(false);
    tree->clear();

    int hidden = 0;
    QTreeWidgetItem *select = nullptr;
    for (int i = 0; i < entries.size(); i++) {
        const Entry &e = entries[i];
        if (!e.id.runnable() && !showAll->isChecked()) {
            hidden++;
            continue;
        }
        auto *it = new QTreeWidgetItem(tree);
        it->setText(ColRelease, e.id.release.isEmpty() ? tr("(not recognised)") : e.id.release);
        it->setText(ColVersion, e.id.version);
        it->setText(ColProfile, e.id.runnable() ? profileName(e.userProfile >= 0 ? e.userProfile : e.id.profile) : QString());
        it->setText(ColFile, e.path);
        it->setToolTip(ColFile, e.path);
        it->setData(0, Qt::UserRole, i);
        if (!e.id.runnable()) {
            for (int c = 0; c < ColCount; c++)
                it->setForeground(c, palette().color(QPalette::Disabled, QPalette::Text));
        }
        if (e.path.compare(cur, Qt::CaseInsensitive) == 0)
            select = it;
    }
    tree->setSortingEnabled(true);
    tree->sortByColumn(ColRelease, Qt::AscendingOrder);
    for (int c = 0; c < ColFile; c++)
        tree->resizeColumnToContents(c);

    const int shown = tree->topLevelItemCount();
    status->setText(hidden ? tr("%1 images, %2 hidden").arg(shown).arg(hidden) : tr("%1 images").arg(shown));
    /* The image this cabinet runs, or else the first one, so Run works at once. */
    if (!select)
        select = tree->topLevelItem(0);
    if (select)
        tree->setCurrentItem(select);
    QTimer::singleShot(0, tree, qOverload<>(&QWidget::setFocus)); /* after the dialog is shown */
    selectionChanged();
}

MachineManager::Entry *
MachineManager::current()
{
    const auto *it = tree->currentItem();
    if (!it)
        return nullptr;
    const int i = it->data(0, Qt::UserRole).toInt();
    return ((i >= 0) && (i < entries.size())) ? &entries[i] : nullptr;
}

void
MachineManager::fillKeys(const QString &want)
{
    key->clear();
    key->addItem(tr("(no key)"), QString());
    for (const MtKeyChoice &c : mt_key_choices())
        key->addItem(c.name, c.ref);
    if (!want.isEmpty() && (key->findData(want) < 0))
        key->addItem(mt_key_display(want), want); /* the user's own file, elsewhere */
    const int i = key->findData(want);
    key->setCurrentIndex((i >= 0) ? i : 0);
}

void
MachineManager::selectionChanged()
{
    Entry *e = current();
    const bool ok = e && e->id.runnable();
    run->setEnabled(ok);
    profile->setEnabled(ok);
    key->setEnabled(ok);
    if (!e) {
        board->setEnabled(false);
        details->clear();
        return;
    }

    const int p = (e->userProfile >= 0) ? e->userProfile : e->id.profile;
    profile->setCurrentIndex(qMax(0, profile->findData(p)));
    board->setCurrentIndex(qMax(0, board->findData(e->userBoard)));
    board->setEnabled(ok && MT_IS_MAXX(p));
    fillKeys(e->keySet ? e->userKey : mt_default_key(e->id));

    QStringList lines;
    if (!e->id.evidence.isEmpty())
        lines << tr("Identified from %1.").arg(e->id.evidence);
    if (!e->id.note.isEmpty())
        lines << e->id.note;
    if (ok && (key->currentIndex() == 0) && !e->id.keyPrefix.isEmpty())
        lines << tr("No %1 key is built in; fit your own dump.").arg(e->id.keyPrefix);
    details->setText(lines.join(' '));
}

void
MachineManager::accept()
{
    Entry *e = current();
    if (!e || !e->id.runnable())
        return;

    chosenPath    = e->path;
    chosenProfile = profile->currentData().toInt();
    chosenBoard   = MT_IS_MAXX(chosenProfile) ? board->currentData().toInt() : MT_BOARD_DEFAULT;
    chosenKey     = key->currentData().toString();
    chosenTitle   = e->id.version.isEmpty() ? e->id.release : QString("%1 %2").arg(e->id.release, e->id.version);
    QDialog::accept();
}

void
MachineManager::applySelection()
{
    if (chosenPath.isEmpty())
        return;
    megatouch_select(chosenProfile, chosenPath.toUtf8().constData(), chosenKey.toUtf8().constData(),
                     chosenBoard, chosenTitle.toUtf8().constData());
    megatouch_apply_profile();
    config_save();
}

bool
MachineManager::currentImageUsable()
{
    const QString img = QString::fromUtf8(megatouch_image());
    return !img.isEmpty() && QFileInfo(img).isFile();
}
