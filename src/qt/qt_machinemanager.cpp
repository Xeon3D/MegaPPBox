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
#include "qt_networksettings.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/megatouch.h>
#include <86box/megatouch_keys.h>
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

/* ---- Importing: copies of the user's keys and images, never the originals moved ---- */

QStringList
mt_import_keys(QWidget *parent)
{
    QStringList imported;
    const QStringList files = QFileDialog::getOpenFileNames(parent, QObject::tr("Import key dumps"), QString(),
                                                            QObject::tr("Key dumps (*)"));
    if (files.isEmpty())
        return imported;
    QDir().mkpath(mt_keys_dir());

    QStringList report;
    for (const QString &fn : files) {
        const QString base = QFileInfo(fn).fileName();
        QFile         f(fn);
        if (!f.open(QIODevice::ReadOnly) || !mt_key_kind((size_t) f.size())) {
            report << QObject::tr("%1: not a key dump (a keyflasher DS1991 dump is 264 bytes, a DS1205 MultiKey 192).")
                          .arg(base);
            continue;
        }
        const QByteArray d = f.readAll();
        const auto      *data = reinterpret_cast<const uint8_t *>(d.constData());

        /* Which release: read from the dump itself.  A family the file name
           gives settles a dump two releases could share. */
        const char *cand[8];
        const int   n     = mt_key_identify(data, (size_t) d.size(), cand, 8);
        const auto *named = mt_key_family_from_name(base.toUtf8().constData());
        QString     prefix;
        for (int i = 0; i < n; i++)
            if (named && !strcmp(cand[i], named->prefix))
                prefix = QString::fromLatin1(cand[i]);
        if (prefix.isEmpty() && (n == 1))
            prefix = QString::fromLatin1(cand[0]);
        if (prefix.isEmpty()) {
            /* Shared by two releases, or a key this build cannot read: ask. */
            QStringList items;
            QStringList prefixes;
            for (int i = 0; i < n; i++)
                prefixes << QString::fromLatin1(cand[i]);
            if (!n)
                for (const mt_key_family_t *k = mt_key_families; k->prefix; k++)
                    prefixes << QString::fromLatin1(k->prefix);
            for (const QString &p : prefixes)
                items << QString("%1 (%2)").arg(QString::fromUtf8(mt_key_family_find(p.toLatin1().constData())->releases), p);
            bool          ok = false;
            const QString pick = QInputDialog::getItem(parent, QObject::tr("Import key"),
                                                       n ? QObject::tr("%1 fits more than one release. Which is it for?").arg(base)
                                                         : QObject::tr("%1 is not a key this version can read. Which release is it for?").arg(base),
                                                       items, 0, false, &ok);
            if (!ok)
                continue;
            prefix = prefixes.value(items.indexOf(pick));
        }

        char name[64];
        if (!mt_key_file_name(data, (size_t) d.size(), prefix.toLatin1().constData(), name, sizeof(name)))
            continue;
        const QString dest = QDir(mt_keys_dir()).filePath(QString::fromLatin1(name));
        QFile         out(dest);
        if (QFileInfo::exists(dest)) {
            QFile old(dest);
            if (old.open(QIODevice::ReadOnly) && (old.readAll() == d)) {
                report << QObject::tr("%1: already imported, as %2.").arg(base, QString::fromLatin1(name));
                imported << mt_own_key_ref(QString::fromLatin1(name));
                continue;
            }
        }
        if (!out.open(QIODevice::WriteOnly) || (out.write(d) != d.size())) {
            report << QObject::tr("%1: could not write %2.").arg(base, QDir::toNativeSeparators(dest));
            continue;
        }
        report << QObject::tr("%1: %2.").arg(base, mt_key_display(QString::fromLatin1(name)));
        imported << mt_own_key_ref(QString::fromLatin1(name));
    }
    QMessageBox::information(parent, QObject::tr("Import key"),
                             QObject::tr("Keys go to %1.").arg(QDir::toNativeSeparators(mt_keys_dir())) + "\n\n" + report.join('\n'));
    return imported;
}

QStringList
mt_import_images(QWidget *parent, const QString &dir)
{
    QStringList imported;
    const QStringList files = QFileDialog::getOpenFileNames(parent, QObject::tr("Import Megatouch images"), QString(),
                                                            QObject::tr("Disk and CD images (%1);;All files (*)").arg(imageFilters.join(' ')));
    if (files.isEmpty())
        return imported;
    QDir().mkpath(dir);

    qint64 total = 0;
    for (const QString &fn : files)
        total += QFileInfo(fn).size();
    QProgressDialog progress(QObject::tr("Copying images…"), QObject::tr("Stop"), 0, 1000, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    QStringList report;
    qint64      done = 0;
    for (const QString &fn : files) {
        const QFileInfo src(fn);
        const QString   dest = QDir(dir).filePath(src.fileName());
        if (QFileInfo(dest).absoluteFilePath().compare(src.absoluteFilePath(), Qt::CaseInsensitive) == 0) {
            report << QObject::tr("%1: already in the folder.").arg(src.fileName());
            done += src.size();
            continue;
        }
        if (QFileInfo::exists(dest)) {
            report << QObject::tr("%1: a file of that name is already in the folder; not copied.").arg(src.fileName());
            done += src.size();
            continue;
        }
        QFile in(fn);
        QFile out(dest + ".part");
        if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
            report << QObject::tr("%1: could not be copied.").arg(src.fileName());
            continue;
        }
        progress.setLabelText(QObject::tr("Copying %1…").arg(src.fileName()));
        bool       ok = true;
        QByteArray buf;
        while (!in.atEnd()) {
            buf = in.read(8 << 20);
            if (buf.isEmpty() || (out.write(buf) != buf.size())) {
                ok = false;
                break;
            }
            done += buf.size();
            progress.setValue(total ? (int) (done * 1000 / total) : 0);
            if (progress.wasCanceled()) {
                ok = false;
                break;
            }
        }
        out.close();
        if (!ok || !QFile::rename(dest + ".part", dest)) {
            QFile::remove(dest + ".part");
            report << (progress.wasCanceled() ? QObject::tr("%1: stopped.").arg(src.fileName())
                                              : QObject::tr("%1: could not be copied.").arg(src.fileName()));
            if (progress.wasCanceled())
                break;
            continue;
        }
        imported << dest;
    }
    progress.setValue(1000);
    if (!report.isEmpty())
        QMessageBox::information(parent, QObject::tr("Import images"), report.join('\n'));
    return imported;
}

/* The keys: what is installed (built in, and imported into the keys folder), the
   releases each runs, and Import / Delete. */
void
mt_show_keys(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Keys"));
    dlg.resize(760, 420);

    auto *list = new QTreeWidget;
    list->setColumnCount(4);
    list->setHeaderLabels({ QObject::tr("Runs"), QObject::tr("Key"), QObject::tr("Type"), QObject::tr("File") });
    list->setRootIsDecorated(false);
    list->setUniformRowHeights(true);
    list->setSortingEnabled(true);
    list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    auto *where = new QLabel(QObject::tr("Imported keys are kept in %1.").arg(QDir::toNativeSeparators(mt_keys_dir())));
    where->setWordWrap(true);
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto fill = [list]() {
        list->clear();
        auto add = [list](const QString &file, int len, bool builtin, const QString &shown) {
            const QByteArray      name = file.toUtf8();
            const mt_key_family_t *f   = mt_key_family_from_name(name.constData());
            QString which = f ? file.mid((int) strlen(f->prefix) + 1) : file;
            which.remove(QRegularExpression(QStringLiteral("^(full|multikey)_")));
            auto *it = new QTreeWidgetItem(list);
            it->setText(0, f ? QString::fromUtf8(f->releases) : QObject::tr("(unknown release)"));
            it->setText(1, builtin ? shown : which);
            it->setText(2, (mt_key_kind((size_t) len) == MT_KEY_MULTIKEY) ? QObject::tr("DS1205 MultiKey") : QObject::tr("DS1991"));
            it->setText(3, builtin ? QObject::tr("built in") : file);
            it->setData(0, Qt::UserRole, builtin ? QString() : file);
        };
        for (const mt_builtin_key_t *k = mt_builtin_keys; k->id; k++)
            add(QString::fromLatin1(k->id), k->len, true, QString::fromUtf8(k->name));
        for (const QFileInfo &fi : QDir(mt_keys_dir()).entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase))
            if (mt_key_kind((size_t) fi.size()))
                add(fi.fileName(), (int) fi.size(), false, QString());
        list->sortItems(0, Qt::AscendingOrder);
        for (int c = 1; c < 4; c++)
            list->resizeColumnToContents(c);
    };
    fill();

    auto *buttons  = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *importBt = buttons->addButton(QObject::tr("&Import…"), QDialogButtonBox::ActionRole);
    auto *deleteBt = buttons->addButton(QObject::tr("&Delete"), QDialogButtonBox::ActionRole);
    auto *folderBt = buttons->addButton(QObject::tr("Open &folder"), QDialogButtonBox::ActionRole);
    deleteBt->setEnabled(false);
    QObject::connect(list, &QTreeWidget::itemSelectionChanged, &dlg, [list, deleteBt]() {
        const auto *it = list->currentItem();
        deleteBt->setEnabled(it && !it->data(0, Qt::UserRole).toString().isEmpty());
    });
    QObject::connect(importBt, &QPushButton::clicked, &dlg, [&dlg, fill]() {
        if (!mt_import_keys(&dlg).isEmpty())
            fill();
    });
    QObject::connect(deleteBt, &QPushButton::clicked, &dlg, [&dlg, list, fill]() {
        const auto *it = list->currentItem();
        const QString file = it ? it->data(0, Qt::UserRole).toString() : QString();
        if (file.isEmpty())
            return;
        if (QMessageBox::question(&dlg, QObject::tr("Delete key"),
                                  QObject::tr("Delete %1 from the keys folder?").arg(file))
            != QMessageBox::Yes)
            return;
        QFile::remove(QDir(mt_keys_dir()).filePath(file));
        fill();
    });
    QObject::connect(folderBt, &QPushButton::clicked, &dlg, []() {
        QDir().mkpath(mt_keys_dir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(mt_keys_dir()));
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(list, 1);
    layout->addWidget(where);
    layout->addWidget(buttons);
    dlg.exec();
}

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
    auto *importBtn = new QPushButton(tr("&Import images…"));
    importBtn->setToolTip(tr("Copy disk (.img) and CD (.iso) images into this folder"));
    top->addWidget(new QLabel(tr("Images:")));
    top->addWidget(folder, 1);
    top->addWidget(browseBtn);
    top->addWidget(scanBtn);
    top->addWidget(importBtn);

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
    auto *importKeyBtn = new QPushButton(tr("Import &key…"));
    importKeyBtn->setToolTip(tr("Copy key dumps into the keys folder; the release each is for is read from the dump"));
    auto *keysBtn = new QPushButton(tr("K&eys…"));
    keysBtn->setToolTip(tr("The keys installed, and the releases each runs"));
    auto *keyRow = new QHBoxLayout;
    keyRow->addWidget(key, 1);
    keyRow->addWidget(importKeyBtn);
    keyRow->addWidget(keysBtn);
    modemBox   = new QCheckBox(tr("Modem on COM2 (ActionTec 56K)"));
    networkBox = new QCheckBox;
    /* What they plug into: the modem's telephone line (every image), the
       card's network -- NAT, or Mega-Link (this image). */
    modemCfg   = new QPushButton(tr("Modem settings..."));
    networkCfg = new QPushButton(tr("Network settings..."));
    modemCfg->setEnabled(false);
    showNetworkCard(-1);
    auto *modemRow = new QHBoxLayout;
    modemRow->addWidget(modemBox, 1);
    modemRow->addWidget(modemCfg);
    auto *networkRow = new QHBoxLayout;
    networkRow->addWidget(networkBox, 1);
    networkRow->addWidget(networkCfg);
    details = new QLabel;
    details->setWordWrap(true);
    details->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *form = new QFormLayout;
    form->addRow(tr("Hardware profile:"), profile);
    form->addRow(tr("Motherboard:"), board);
    form->addRow(tr("Key:"), keyRow);
    form->addRow(tr("Options:"), modemRow);
    form->addRow(QString(), networkRow);
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
    connect(importBtn, &QPushButton::clicked, this, [this]() {
        if (folder->text().trimmed().isEmpty() || !QFileInfo(folder->text().trimmed()).isDir()) {
            browse();
            if (folder->text().trimmed().isEmpty())
                return;
        }
        if (!mt_import_images(this, folder->text().trimmed()).isEmpty())
            scan();
    });
    connect(keysBtn, &QPushButton::clicked, this, [this]() {
        mt_show_keys(this);
        selectionChanged(); /* keys may have come or gone */
    });
    connect(importKeyBtn, &QPushButton::clicked, this, [this]() {
        /* The current image takes its release's key, unless the user chose one. */
        if (!mt_import_keys(this).isEmpty())
            selectionChanged();
    });
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
            showNetworkCard(e->userProfile);
        }
    });
    connect(modemCfg, &QPushButton::clicked, this, [this]() { mt_configure_modem(this); });
    /* The image's network settings; the box above is kept with the image
       first, so the dialog starts from it, and follows what the dialog chose
       ("None" is no card). */
    connect(networkCfg, &QPushButton::clicked, this, [this]() {
        Entry *e = current();
        if (!e)
            return;
        const QByteArray path = e->path.toUtf8();
        megatouch_set_image_option(path.constData(), MT_OPT_NETWORK, e->network > 0);
        if (e->id.link485)
            megatouch_network_default_card(path.constData(), "link485");
        config_save();
        NetworkSettings dialog(this, e->path, profile->currentData().toInt());
        if (dialog.exec() == QDialog::Accepted) {
            config_save();
            e->network = megatouch_image_option(path.constData(), MT_OPT_NETWORK);
            networkBox->setChecked(e->network > 0);
        }
    });
    connect(modemBox, &QCheckBox::clicked, this, [this](bool on) {
        if (Entry *e = current()) {
            e->modem       = on;
            e->optsChanged = true;
        }
    });
    connect(networkBox, &QCheckBox::clicked, this, [this](bool on) {
        if (Entry *e = current()) {
            e->network     = on;
            e->optsChanged = true;
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

/* The card the release and profile take, in the network option's text: the
   XL releases and MAXX 1st link over RS-485 on COM2, the later MAXX
   releases over Ethernet. */
void
MachineManager::showNetworkCard(int p)
{
    const Entry *e    = current();
    const bool   r485 = !MT_IS_MAXX(p) || (e && e->id.link485 && (p == MT_PROFILE_MAXX_OLD));

    if (r485)
        networkBox->setText(tr("Mega-Link (RS-485 on COM2)"));
    else if (p == MT_PROFILE_MAXX_OLD)
        networkBox->setText(tr("Network card (TRENDnet TE-16XP)"));
    else
        networkBox->setText(tr("Network card (Realtek RTL8139)"));
    networkBox->setEnabled(e && e->id.runnable());
    networkCfg->setEnabled(e && e->id.runnable());
}

void
MachineManager::selectionChanged()
{
    Entry *e = current();
    const bool ok = e && e->id.runnable();
    run->setEnabled(ok);
    profile->setEnabled(ok);
    key->setEnabled(ok);
    modemBox->setEnabled(ok);
    modemCfg->setEnabled(ok);
    if (!e) {
        board->setEnabled(false);
        networkBox->setEnabled(false);
        networkCfg->setEnabled(false);
        showNetworkCard(-1);
        modemBox->setChecked(false);
        networkBox->setChecked(false);
        details->clear();
        return;
    }

    const int p = (e->userProfile >= 0) ? e->userProfile : e->id.profile;
    profile->setCurrentIndex(qMax(0, profile->findData(p)));
    board->setCurrentIndex(qMax(0, board->findData(e->userBoard)));
    board->setEnabled(ok && MT_IS_MAXX(p));
    if (e->modem < 0) {
        /* The image's own choice, else on for a release with an on-line client. */
        const int saved = megatouch_image_option_saved(e->path.toUtf8().constData(), MT_OPT_MODEM);
        e->modem = (saved < 0) ? e->id.modem : saved;
    }
    if (e->network < 0)
        e->network = megatouch_image_option(e->path.toUtf8().constData(), MT_OPT_NETWORK);
    modemBox->setChecked(e->modem > 0);
    networkBox->setChecked(e->network > 0);
    showNetworkCard(p);
    fillKeys(e->keySet ? e->userKey : mt_default_key(e->id));

    QStringList lines;
    if (!e->id.evidence.isEmpty())
        lines << tr("Identified from %1.").arg(e->id.evidence);
    if (!e->id.note.isEmpty())
        lines << e->id.note;
    if (ok && (key->currentIndex() == 0) && !e->id.keyPrefix.isEmpty())
        lines << tr("No %1 key yet: Import key… copies in a dump of one.").arg(e->id.keyPrefix);
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

    /* Options changed on any image are kept with that image; the chosen
       image's always are, so that a release's default (the modem, from
       Diamond on) is what the emulator builds, not only what the box shows. */
    for (const Entry &x : entries) {
        if (!x.optsChanged && (&x != e))
            continue;
        const QByteArray path = x.path.toUtf8();
        megatouch_set_image_option(path.constData(), MT_OPT_MODEM, x.modem > 0);
        megatouch_set_image_option(path.constData(), MT_OPT_NETWORK, x.network > 0);
        if (x.id.link485 && (x.network > 0))
            megatouch_network_default_card(path.constData(), "link485");
    }
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
