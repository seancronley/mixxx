#include <QMessageBox>
#include <QtDebug>
#include <QXmlStreamReader>
#include <QDesktopServices>
#include <QFileDialog>
#include <QMenu>
#include <QAction>
#include <QList>

#include "library/banshee/bansheefeature.h"

#include "library/banshee/bansheedbconnection.h"
#include "library/dao/settingsdao.h"
#include "library/baseexternalplaylistmodel.h"
#include "library/banshee/bansheeplaylistmodel.h"


const QString BansheeFeature::BANSHEE_MOUNT_KEY = "mixxx.BansheeFeature.mount";
QString BansheeFeature::m_databaseFile;

BansheeFeature::BansheeFeature(QObject* parent, TrackCollection* pTrackCollection, ConfigObject<ConfigValue>* pConfig)
        : BaseExternalLibraryFeature(parent, pTrackCollection),
          m_pTrackCollection(pTrackCollection),
          m_cancelImport(false) {
    Q_UNUSED(pConfig);
 //   m_pBansheePlaylistModel = new BansheePlaylistModel(this, m_pTrackCollection, &m_connection);
    m_isActivated = false;
    m_title = tr("Banshee");
}

BansheeFeature::~BansheeFeature() {
    qDebug() << "~BansheeFeature()";
    // stop import thread, if still running
    m_cancelImport = true;
    if (m_future.isRunning()) {
        qDebug() << "m_future still running";
        m_future.waitForFinished();
        qDebug() << "m_future finished";
    }

    delete m_pBansheePlaylistModel;
}

BaseSqlTableModel* BansheeFeature::getPlaylistModelForPlaylist(QString playlist) {
    BaseExternalPlaylistModel* pModel = new BaseExternalPlaylistModel(
        this, m_pTrackCollection,
        "mixxx.db.model.itunes_playlist",
        "itunes_playlists",
        "itunes_playlist_tracks",
        "itunes");
    pModel->setPlaylist(playlist);
    return pModel;
}

// static
bool BansheeFeature::isSupported() {
    return !m_databaseFile.isEmpty();
}

// static
void BansheeFeature::prepareDbPath(ConfigObject<ConfigValue>* pConfig) {
    m_databaseFile = pConfig->getValueString(ConfigKey("[Banshee]","Database"));
    if (!QFile::exists(m_databaseFile)) {
        // Fall back to default
        m_databaseFile = BansheeDbConnection::getDatabaseFile();
    }
}

QVariant BansheeFeature::title() {
    return m_title;
}

QIcon BansheeFeature::getIcon() {
    return QIcon(":/images/library/ic_library_banshee.png");
}

void BansheeFeature::activate() {
    //qDebug("BansheeFeature::activate()");

    if (!m_isActivated) {

        if (!QFile::exists(m_databaseFile)) {
            // Fall back to default
            m_databaseFile = BansheeDbConnection::getDatabaseFile();
        }

        if (!QFile::exists(m_databaseFile)) {
            // TODO(dsc): error handling
            qDebug() << m_databaseFile << "does not exist";
        }

        if (!m_connection.open(m_databaseFile)) {
            QMessageBox::warning(
                    NULL,
                    tr("Error Loading Banshee database"),
                    tr("There was an error loading your Banshee database at\n") +
                    m_databaseFile);
            return;
        }

        qDebug() << "Using Banshee Database Schema V" << m_connection.getSchemaVersion();

        m_isActivated =  true;

        qDebug() << "BansheeFeature::importLibrary() ";

        TreeItem* playlist_root = new TreeItem();

        QList<struct BansheeDbConnection::Playlist> list = m_connection.getPlaylists();

        struct BansheeDbConnection::Playlist playlist;
        foreach (playlist, list) {
            qDebug() << playlist.name;
            // append the playlist to the child model
            TreeItem *item = new TreeItem(playlist.name, playlist.playlistId, this, playlist_root);
            playlist_root->appendChild(item);
        };

        if(playlist_root){
            m_childModel.setRootItem(playlist_root);
            if (m_isActivated) {
                activate();
            }
            qDebug() << "Banshee library loaded: success";
        }

        //calls a slot in the sidebarmodel such that 'isLoading' is removed from the feature title.
        m_title = tr("Banshee");
        emit(featureLoadingFinished(this));
    }

    m_pBansheePlaylistModel->setPlaylist(0); // Gets the master playlist
    emit(showTrackModel(m_pBansheePlaylistModel));
}

void BansheeFeature::activateChild(const QModelIndex& index) {
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    //qDebug() << "BansheeFeature::activateChild " << item->data() << " " << item->dataPath();
    QString playlist = item->dataPath().toString();
    int playlistID = playlist.toInt();
    if (playlistID > 0) {
        qDebug() << "Activating " << item->data().toString();
        m_pBansheePlaylistModel->setPlaylist(playlistID);
        emit(showTrackModel(m_pBansheePlaylistModel));
    }
}

TreeItemModel* BansheeFeature::getChildModel() {
    return &m_childModel;
}

void BansheeFeature::addToAutoDJ(bool bTop) {
    // qDebug() << "slotAddToAutoDJ() row:" << m_lastRightClickedIndex.data();

    if (m_lastRightClickedIndex.isValid()) {
        TreeItem *item = static_cast<TreeItem*>(m_lastRightClickedIndex.internalPointer());
        qDebug() << "BansheeFeature::addToAutoDJ " << item->data() << " " << item->dataPath();
        QString playlist = item->dataPath().toString();
        int playlistID = playlist.toInt();
        if (playlistID > 0) {
//            BansheePlaylistModel* pPlaylistModelToAdd = new BansheePlaylistModel(this, m_pTrackCollection, &m_connection);
//            pPlaylistModelToAdd->setPlaylist(playlistID);
            PlaylistDAO &playlistDao = m_pTrackCollection->getPlaylistDAO();
            int autoDJId = playlistDao.getPlaylistIdFromName(AUTODJ_TABLE);

            int rows =
                    0;
 //                   pPlaylistModelToAdd->rowCount();
            for(int i = 0; i < rows; ++i){
                QModelIndex index; // = pPlaylistModelToAdd->index(i,0);
                if (index.isValid()) {
                    TrackPointer track; // = pPlaylistModelToAdd->getTrack(index);
                    if (bTop) {
                        // Start at position 2 because position 1 was already loaded to the deck
                        playlistDao.insertTrackIntoPlaylist(track->getId(), autoDJId, i+2);
                    } else {
                        playlistDao.appendTrackToPlaylist(track->getId(), autoDJId);
                    }
                }
            }
//            delete pPlaylistModelToAdd;
        }
    }
}

void BansheeFeature::slotImportAsMixxxPlaylist() {
    // qDebug() << "slotAddToAutoDJ() row:" << m_lastRightClickedIndex.data();

    if (m_lastRightClickedIndex.isValid()) {
        TreeItem *item = static_cast<TreeItem*>(m_lastRightClickedIndex.internalPointer());
        QString playlistName = item->data().toString();
        QString playlistStId = item->dataPath().toString();
        int playlistID = playlistStId.toInt();
        qDebug() << "BansheeFeature::slotImportAsMixxxPlaylist " << playlistName << " " << playlistStId;
        if (playlistID > 0) {
 //           BansheePlaylistModel* pPlaylistModelToAdd = new BansheePlaylistModel(this, m_pTrackCollection, &m_connection);
 //           pPlaylistModelToAdd->setPlaylist(playlistID);
            PlaylistDAO &playlistDao = m_pTrackCollection->getPlaylistDAO();

            int playlistId = playlistDao.getPlaylistIdFromName(playlistName);
            int i = 1;

            if (playlistId != -1) {
                // Calculate a unique name
                playlistName += "(%1)";
                while (playlistId != -1) {
                    i++;
                    playlistId = playlistDao.getPlaylistIdFromName(playlistName.arg(i));
                }
                playlistName = playlistName.arg(i);
            }
            playlistId = playlistDao.createPlaylist(playlistName);

            if (playlistId != -1) {
                // Copy Tracks
                int rows = 0; //= pPlaylistModelToAdd->rowCount();
                for (int i = 0; i < rows; ++i) {
                    QModelIndex index; // = pPlaylistModelToAdd->index(i,0);
                    if (index.isValid()) {
                        //qDebug() << pPlaylistModelToAdd->getTrackLocation(index);
//                        TrackPointer track = pPlaylistModelToAdd->getTrack(index);
//                        playlistDao.appendTrackToPlaylist(track->getId(), playlistId);
                    }
                }
            }
            else {
                QMessageBox::warning(NULL,
                                     tr("Playlist Creation Failed"),
                                     tr("An unknown error occurred while creating playlist: ")
                                      + playlistName);
            }

 //           delete pPlaylistModelToAdd;
        }
    }
}
