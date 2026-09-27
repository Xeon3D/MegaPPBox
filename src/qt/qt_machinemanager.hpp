/*
 * MegaPPBox - Merit Megatouch cabinets on 86Box.
 *
 *          The Machine Manager: scans a folder of disk and CD images, says
 *          what each one is, and runs the chosen one on the hardware profile
 *          and key it wants.
 *
 * Authors: MegaPPBox contributors
 *
 *          Released under the GNU General Public License version 2 or
 *          later.  See COPYING for more information.
 */
#ifndef QT_MACHINEMANAGER_HPP
#define QT_MACHINEMANAGER_HPP

#include <QDialog>
#include <QList>
#include <QString>

#include "qt_megatouch_ident.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class MachineManager : public QDialog {
    Q_OBJECT

public:
    explicit MachineManager(QWidget *parent = nullptr);

    /* Whether the configured image can be booted as it is. */
    static bool currentImageUsable();

    /* Make the accepted choice this cabinet's pick (and save it).  The caller
       hard-resets if the machine is already running. */
    void applySelection();

    struct Entry {
        QString path;
        qint64  size = 0;
        qint64  mtime = 0;
        MtIdent id;
        /* What the user changed for this image, remembered with it. */
        int     userProfile = -1;
        int     userBoard   = 0;
        QString userKey;
        bool    keySet = false; /* userKey is a choice, even if empty (no key) */
    };

private slots:
    void browse();
    void scan();
    void selectionChanged();
    void accept() override;

private:
    void    populate();
    void    fillKeys(const QString &want);
    QString keysDir() const;
    Entry  *current();

    QLineEdit   *folder;
    QTreeWidget *tree;
    QCheckBox   *showAll;
    QComboBox   *profile;
    QComboBox   *board;
    QComboBox   *key;
    QLabel      *details;
    QLabel      *status;
    QPushButton *run;

    QList<Entry> entries;

    QString chosenPath;
    QString chosenTitle;
    QString chosenKey;
    int     chosenProfile = -1;
    int     chosenBoard   = 0;
};

#endif
