#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qvoptionsdialog.h"
#include "qvaboutdialog.h"
#include "qvwelcomedialog.h"
#include "qvapplication.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QString>
#include <QGraphicsPixmapItem>
#include <QPixmap>
#include <QClipboard>
#include <QCoreApplication>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QDesktopServices>
#include <QContextMenuEvent>
#include <QMovie>
#include <QImageWriter>
#include <QSettings>
#include <QStyle>
#include <QIcon>
#include <QMimeDatabase>
#include <QScreen>
#include <QCursor>
#include <QInputDialog>
#include <QProgressDialog>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QMenu>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);

    //initialize variables
    menuBarEnabled = false;
    slideshowReversed = false;
    windowResizeMode = 0;
    justLaunchedWithImage = false;
    maxWindowResizedPercentage = 0.7;

    //connect graphicsview signals
    connect(ui->graphicsView, &QVGraphicsView::fileLoaded, this, &MainWindow::fileLoaded);
    connect(ui->graphicsView, &QVGraphicsView::updatedLoadedPixmapItem, this, &MainWindow::setWindowSize);
    connect(ui->graphicsView, &QVGraphicsView::updatedFileInfo, this, &MainWindow::refreshProperties);
    connect(ui->graphicsView, &QVGraphicsView::sendWindowTitle, this, &MainWindow::setWindowTitle);
    connect(ui->graphicsView, &QVGraphicsView::cancelSlideshow, this, &MainWindow::cancelSlideshow);

    //Initialize escape shortcut
    escShortcut = new QShortcut(Qt::Key_Escape, this);
    connect(escShortcut, &QShortcut::activated, this, [this](){
        if (windowState() == Qt::WindowFullScreen)
            toggleFullScreen();
    });

    //Enable drag&dropping
    setAcceptDrops(true);

    //Make info dialog object
    info = new QVInfoDialog(this);

    //Timer for slideshow
    slideshowTimer = new QTimer(this);
    connect(slideshowTimer, &QTimer::timeout, this, &MainWindow::slideshowAction);

    //Load window geometry
    QSettings settings;
    restoreGeometry(settings.value("geometry").toByteArray());

    //Context menu
    auto *actionManager = qvApp->getActionManager();

    contextMenu = new QMenu(this);

    contextMenu->addAction(actionManager->cloneAction("open"));
    contextMenu->addAction(actionManager->cloneAction("openurl"));
    contextMenu->addMenu(actionManager->buildRecentsMenu(true, contextMenu));
    contextMenu->addAction(actionManager->cloneAction("opencontainingfolder"));
    contextMenu->addAction(actionManager->cloneAction("showfileinfo"));
    contextMenu->addSeparator();
    contextMenu->addAction(actionManager->cloneAction("copy"));
    contextMenu->addAction(actionManager->cloneAction("paste"));
    contextMenu->addSeparator();
    contextMenu->addAction(actionManager->cloneAction("nextfile"));
    contextMenu->addAction(actionManager->cloneAction("previousfile"));
    contextMenu->addSeparator();
    contextMenu->addMenu(actionManager->buildViewMenu(true, contextMenu));
    contextMenu->addMenu(actionManager->buildToolsMenu(contextMenu));
    contextMenu->addMenu(actionManager->buildHelpMenu(contextMenu));

    connect(contextMenu, &QMenu::triggered, [this](QAction *triggeredAction){
        qvApp->getActionManager()->actionTriggered(triggeredAction, this);
    });

    // Initialize menubar
    setMenuBar(actionManager->buildMenuBar(this));
    connect(menuBar(), &QMenuBar::triggered, [this](QAction *triggeredAction){
        qvApp->getActionManager()->actionTriggered(triggeredAction, this);
    });

    // Add all actions to this window so keyboard shortcuts are always triggered
    // using virtual menu to hold them so i can connect to the triggered signal
    virtualMenu = new QMenu(this);
    virtualMenu->addActions(actionManager->getActionLibrary().values());
    addActions(virtualMenu->actions());
    connect(virtualMenu, &QMenu::triggered, [this](QAction *triggeredAction){
       qvApp->getActionManager()->actionTriggered(triggeredAction, this);
    });

    loadSettings();
}

MainWindow::~MainWindow()
{
    delete ui;
}

bool MainWindow::event(QEvent *event)
{
    if (event->type() == QEvent::WindowActivate)
    {
        qvApp->addToLastActiveWindows(this);
    }
    return QMainWindow::event(event);
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMainWindow::contextMenuEvent(event);
    contextMenu->popup(event->globalPos());
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    QSettings settings;

    if (settings.value("firstlaunch", false).toBool())
        return;

    settings.setValue("firstlaunch", true);
    settings.setValue("configversion", VERSION);
    QTimer::singleShot(100, this, &MainWindow::openWelcome);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings settings;
    settings.setValue("geometry", saveGeometry());

    qvApp->deleteFromLastActiveWindows(this);
    qvApp->getActionManager()->untrackClonedActions(contextMenu);
    qvApp->getActionManager()->untrackClonedActions(menuBar());

    QMainWindow::closeEvent(event);
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MouseButton::BackButton)
        previousFile();
    else if (event->button() == Qt::MouseButton::ForwardButton)
        nextFile();

    QMainWindow::mousePressEvent(event);
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MouseButton::LeftButton)
        toggleFullScreen();
    QMainWindow::mouseDoubleClickEvent(event);
}

void MainWindow::pickFile()
{
    QSettings settings;
    settings.beginGroup("recents");
    QFileDialog *fileDialog = new QFileDialog(this, tr("Open..."));
    fileDialog->setDirectory(settings.value("lastFileDialogDir", QDir::homePath()).toString());
    fileDialog->setFileMode(QFileDialog::ExistingFiles);
    fileDialog->setNameFilters(qvApp->getNameFilterList());
    fileDialog->open();
    connect(fileDialog, &QFileDialog::filesSelected, [this](const QStringList &selected){
        bool first = true;
        for (const auto &file : selected)
        {
            if (first)
            {
                openFile(file);
                first = false;
                continue;
            }

            QVApplication::openFile(QVApplication::newWindow(), file);
        }
    });
}

void MainWindow::openFile(const QString &fileName)
{
    QSettings settings;
    settings.beginGroup("recents");
    settings.setValue("lastFileDialogDir", QFileInfo(fileName).path());

    ui->graphicsView->loadFile(fileName);
    cancelSlideshow();
}


void MainWindow::loadSettings()
{
    QSettings settings;
    settings.beginGroup("options");

    //menubar
    menuBarEnabled = settings.value("menubarenabled", false).toBool();
    menuBar()->setVisible(menuBarEnabled);
    const auto actionList = actions();
    for (const auto &action : actionList)
    {
        action->setVisible(!menuBarEnabled);
    }

    //slideshow timer
    slideshowTimer->setInterval(static_cast<int>(settings.value("slideshowtimer", 5).toDouble()*1000));

    //slideshow direction
    slideshowReversed = settings.value("slideshowreversed", false).toBool();

    //window resize mode
    windowResizeMode = settings.value("windowresizemode", 1).toInt();

    //max window resize mode size
    maxWindowResizedPercentage = settings.value("maxwindowresizedpercentage", 70).toReal()/100;

    ui->graphicsView->loadSettings();
    qvApp->getActionManager()->loadSettings();

    // If esc is not used in a shortcut, let it exit fullscreen
    escShortcut->setKey(Qt::Key_Escape);

    const auto &actionLibrary = qvApp->getActionManager()->getActionLibrary();
    for (const auto &action : actionLibrary)
    {
        if (action->shortcuts().contains(QKeySequence(Qt::Key_Escape)))
        {
            escShortcut->setKey({});
            break;
        }
    }
}

void MainWindow::openRecent(int i)
{
    auto recentsList = qvApp->getActionManager()->getRecentsList();
    ui->graphicsView->loadFile(recentsList.value(i).filePath);
    cancelSlideshow();
}

void MainWindow::fileLoaded()
{
    //activate items after item is loaded for the first time
//    if (ui->graphicsView->getCurrentFileDetails().isPixmapLoaded && !ui->actionOpen_Containing_Folder->isEnabled())
//    {
//        foreach(QAction* action, ui->menuView->actions())
//            action->setEnabled(true);
//        foreach(QAction* action, contextMenu->actions())
//            action->setEnabled(true);
//        foreach(QAction* action, actions())
//            action->setEnabled(true);
//        ui->actionSlideshow->setEnabled(true);
//        ui->actionCopy->setEnabled(true);
//    }
    //disable gif controls if there is no gif loaded
//    ui->menuTools->actions().constFirst()->setEnabled(ui->graphicsView->getCurrentFileDetails().isMovieLoaded);
}

void MainWindow::refreshProperties()
{
    int value4;
    if (ui->graphicsView->getCurrentFileDetails().isMovieLoaded)
        value4 = ui->graphicsView->getLoadedMovie().frameCount();
    else
        value4 = 0;
    info->setInfo(ui->graphicsView->getCurrentFileDetails().fileInfo, ui->graphicsView->getCurrentFileDetails().imageSize.width(), ui->graphicsView->getCurrentFileDetails().imageSize.height(), value4);

}

void MainWindow::setWindowSize()
{
    //check if the program is configured to resize the window
    if (!(windowResizeMode == 2 || (windowResizeMode == 1 && justLaunchedWithImage)))
        return;

    //check if window is maximized or fullscreened
    if (windowState() == Qt::WindowMaximized || windowState() == Qt::WindowFullScreen)
        return;

    justLaunchedWithImage = false;

    QSize imageSize = ui->graphicsView->getCurrentFileDetails().loadedPixmapSize;
    imageSize -= QSize(4, 4);
    imageSize /= devicePixelRatioF();

    QSize currentScreenSize = screenAt(geometry().center())->size();
    currentScreenSize *= maxWindowResizedPercentage;

    if (imageSize.width() > currentScreenSize.width() || imageSize.height() > currentScreenSize.height())
        imageSize.scale(currentScreenSize, Qt::KeepAspectRatio);

    //Windows reports the wrong minimum width, so we constrain the image size relative to the dpi to stop weirdness with tiny images
    #ifdef Q_OS_WIN
    auto minimumImageSize = QSize(qRound(logicalDpiX()*1.5), logicalDpiY()/2);
    if (imageSize.boundedTo(minimumImageSize) == imageSize)
        imageSize = minimumImageSize;
    #endif

    QRect oldRect = geometry();
    resize(imageSize);
    QRect newRect = geometry();
    newRect.moveCenter(oldRect.center());

    const int titlebarHeight = QApplication::style()->pixelMetric(QStyle::PM_TitleBarHeight);
    if (newRect.y() < titlebarHeight)
        newRect.setY(titlebarHeight);

    setGeometry(newRect);
}

//literally just copy pasted from Qt source code to maintain compatibility with 5.9
QScreen *MainWindow::screenAt(const QPoint &point)
{
    QVarLengthArray<const QScreen *, 8> visitedScreens;
    for (const QScreen *screen : QGuiApplication::screens()) {
        if (visitedScreens.contains(screen))
            continue;
        // The virtual siblings include the screen itself, so iterate directly
        for (QScreen *sibling : screen->virtualSiblings()) {
            if (sibling->geometry().contains(point))
                return sibling;
            visitedScreens.append(sibling);
        }
    }
    return nullptr;
}

bool MainWindow::getIsPixmapLoaded() const
{
    return ui->graphicsView->getCurrentFileDetails().isPixmapLoaded;
}

void MainWindow::setJustLaunchedWithImage(bool value)
{
    justLaunchedWithImage = value;
}

void MainWindow::openUrl(const QUrl &url)
{
    auto *networkManager = new QNetworkAccessManager(this);

    if (!url.isValid()) {
        QMessageBox::critical(this, tr("Error"), tr("Error: URL is invalid"));
        return;
    }

    auto request = QNetworkRequest(url);
    auto *reply = networkManager->get(request);
    auto *progressDialog = new QProgressDialog(tr("Downloading image..."), tr("Cancel"), 0, 100);
    progressDialog->setAutoClose(false);
    progressDialog->setAutoReset(false);
    progressDialog->setWindowTitle(tr("Open URL..."));
    progressDialog->open();

    connect(progressDialog, &QProgressDialog::canceled, [reply]{
        reply->abort();
    });

    connect(reply, &QNetworkReply::downloadProgress, [progressDialog](qreal bytesReceived, qreal bytesTotal){
        auto percent = (bytesReceived/bytesTotal)*100;
        progressDialog->setValue(qRound(percent));
    });


    connect(reply, &QNetworkReply::finished, [networkManager, progressDialog, reply, this]{
        if (reply->error())
        {
            progressDialog->close();
            QMessageBox::critical(this, tr("Error"), tr("Error ") + QString::number(reply->error()) + ": " + reply->errorString());

            networkManager->deleteLater();
            progressDialog->deleteLater();
            return;
        }

        progressDialog->setMaximum(0);

        auto *tempFile = new QTemporaryFile(this);

        auto *saveFutureWatcher = new QFutureWatcher<bool>();
        connect(saveFutureWatcher, &QFutureWatcher<bool>::finished, this, [networkManager, progressDialog, tempFile, saveFutureWatcher, this](){
            progressDialog->close();
            if (saveFutureWatcher->result())
            {
                if (tempFile->open())
                {
                    openFile(tempFile->fileName());
                }
            }
            else
            {
                 QMessageBox::critical(this, tr("Error"), tr("Error: Invalid image"));
                 tempFile->deleteLater();
            }
            progressDialog->deleteLater();
            networkManager->deleteLater();
            saveFutureWatcher->deleteLater();
        });

        saveFutureWatcher->setFuture(QtConcurrent::run([reply, tempFile]{
            return QImage::fromData(reply->readAll()).save(tempFile, "png");
        }));

    });
}

void MainWindow::pickUrl()
{
    auto inputDialog = new QInputDialog(this);
    inputDialog->setWindowTitle("Open URL...");
    inputDialog->setLabelText(tr("URL of a supported image file:"));
    inputDialog->resize(350, inputDialog->height());
    connect(inputDialog, &QInputDialog::finished, [inputDialog, this](int result) {
        if (!result)
            return;

        auto url = QUrl(inputDialog->textValue());
        inputDialog->deleteLater();
        openUrl(url);
    });
    inputDialog->open();
}

void MainWindow::openContainingFolder()
{
    if (!ui->graphicsView->getCurrentFileDetails().isPixmapLoaded)
        return;

    const QFileInfo selectedFileInfo = ui->graphicsView->getCurrentFileDetails().fileInfo;

    #ifdef Q_OS_WIN
    QProcess::startDetached("explorer", QStringList() << "/select," << QDir::toNativeSeparators(selectedFileInfo.absoluteFilePath()));
    #elif defined Q_OS_MACOS
    QProcess::execute("open", QStringList() << "-R" << selectedFileInfo.absoluteFilePath());
    #else
    QDesktopServices::openUrl(selectedFileInfo.absolutePath());
    #endif
}

void MainWindow::showFileInfo()
{
    refreshProperties();
    info->show();
}

void MainWindow::copy()
{
    auto *mimeData = ui->graphicsView->getMimeData();
    if (!mimeData->hasImage() || !mimeData->hasUrls())
    {
        mimeData->deleteLater();
        return;
    }

    QApplication::clipboard()->setMimeData(mimeData);
}

void MainWindow::paste()
{
    const QMimeData *mimeData = QApplication::clipboard()->mimeData();

    if (mimeData->hasText())
    {
        auto url = QUrl(mimeData->text());

        if (url.isValid() && (url.scheme() == "http" || url.scheme() == "https"))
        {
            openUrl(url);
            return;
        }
    }

    ui->graphicsView->loadMimeData(mimeData);
}

void MainWindow::zoomIn()
{
    ui->graphicsView->zoom(120, ui->graphicsView->mapFromGlobal(QCursor::pos()));
}

void MainWindow::zoomOut()
{
    ui->graphicsView->zoom(-120, ui->graphicsView->mapFromGlobal(QCursor::pos()));
}

void MainWindow::resetZoom()
{
    ui->graphicsView->resetScale();
}

void MainWindow::originalSize()
{
    ui->graphicsView->originalSize();
}

void MainWindow::rotateRight()
{
    ui->graphicsView->rotateImage(90);
    resetZoom();
}

void MainWindow::rotateLeft()
{
    ui->graphicsView->rotateImage(-90);
    resetZoom();
}

void MainWindow::mirror()
{
    ui->graphicsView->scale(-1, 1);
    resetZoom();
}

void MainWindow::flip()
{
    ui->graphicsView->scale(1, -1);
    resetZoom();
}

void MainWindow::firstFile()
{
    ui->graphicsView->goToFile(QVGraphicsView::goToFileMode::first);
}

void MainWindow::previousFile()
{
    ui->graphicsView->goToFile(QVGraphicsView::goToFileMode::previous);
}

void MainWindow::nextFile()
{
    ui->graphicsView->goToFile(QVGraphicsView::goToFileMode::next);
}

void MainWindow::lastFile()
{
    ui->graphicsView->goToFile(QVGraphicsView::goToFileMode::last);
}

void MainWindow::openOptions()
{
    auto *options = new QVOptionsDialog(this);
    options->open();

    connect(options, &QVOptionsDialog::optionsSaved, this, &MainWindow::loadSettings);
}

void MainWindow::openAbout()
{
    auto *about = new QVAboutDialog(this);
    about->exec();
}

void MainWindow::openWelcome()
{
    auto *welcome = new QVWelcomeDialog(this);
    welcome->exec();
}

void MainWindow::saveFrameAs()
{
    QSettings settings;
    settings.beginGroup("recents");
    if (!ui->graphicsView->getCurrentFileDetails().isMovieLoaded)
        return;

    if (ui->graphicsView->getLoadedMovie().state() == QMovie::Running)
    {
        pause();
    }
    QFileDialog *saveDialog = new QFileDialog(this, tr("Save Frame As..."));
    saveDialog->setDirectory(settings.value("lastFileDialogDir", QDir::homePath()).toString());
    saveDialog->setNameFilters(qvApp->getNameFilterList());
    saveDialog->selectFile(ui->graphicsView->getCurrentFileDetails().fileInfo.baseName() + "-" + QString::number(ui->graphicsView->getLoadedMovie().currentFrameNumber()) + ".png");
    saveDialog->setDefaultSuffix("png");
    saveDialog->setAcceptMode(QFileDialog::AcceptSave);
    saveDialog->open();
    connect(saveDialog, &QFileDialog::fileSelected, this, [=](const QString &fileName){
        ui->graphicsView->originalSize();
        for(int i=0; i < ui->graphicsView->getLoadedMovie().frameCount(); i++)
            nextFrame();

        ui->graphicsView->getLoadedMovie().currentPixmap().save(fileName, nullptr, 100);
        ui->graphicsView->resetScale();
    });
}

void MainWindow::pause()
{
    if (!ui->graphicsView->getCurrentFileDetails().isMovieLoaded)
        return;

    const auto pauseActions = qvApp->getActionManager()->getAllInstancesOfAction("pause");

    if (ui->graphicsView->getLoadedMovie().state() == QMovie::Running)
    {
        ui->graphicsView->setPaused(true);
        for (const auto &pauseAction : pauseActions)
        {
            pauseAction->setText(tr("Resume"));
            pauseAction->setIcon(QIcon::fromTheme("media-playback-start"));
        }
    }
    else
    {
        ui->graphicsView->setPaused(false);
        for (const auto &pauseAction : pauseActions)
        {
            pauseAction->setText(tr("Pause"));
            pauseAction->setIcon(QIcon::fromTheme("media-playback-pause"));
        }
    }
}

void MainWindow::nextFrame()
{
    if (!ui->graphicsView->getCurrentFileDetails().isMovieLoaded)
        return;

    ui->graphicsView->jumpToNextFrame();
}

void MainWindow::toggleSlideshow()
{
    const auto slideshowActions = qvApp->getActionManager()->getAllInstancesOfAction("slideshow");

    if (slideshowTimer->isActive())
    {
        slideshowTimer->stop();
        for (const auto &slideshowAction : slideshowActions)
        {
            slideshowAction->setText(tr("Start Slideshow"));
            slideshowAction->setIcon(QIcon::fromTheme("media-playback-start"));
        }
    }
    else
    {
        slideshowTimer->start();
        for (const auto &slideshowAction : slideshowActions)
        {
            slideshowAction->setText(tr("Stop Slideshow"));
            slideshowAction->setIcon(QIcon::fromTheme("media-playback-stop"));
        }
    }
}

void MainWindow::cancelSlideshow()
{
    if (slideshowTimer->isActive())
        toggleSlideshow();
}

void MainWindow::slideshowAction()
{
    QSettings settings;
    settings.beginGroup("options");
    if (slideshowReversed)
        previousFile();
    else
        nextFile();
}

void MainWindow::decreaseSpeed()
{
    if (!ui->graphicsView->getCurrentFileDetails().isMovieLoaded)
        return;

    ui->graphicsView->setSpeed(ui->graphicsView->getLoadedMovie().speed()-25);
}

void MainWindow::resetSpeed()
{
    if (!ui->graphicsView->getCurrentFileDetails().isMovieLoaded)
        return;

    ui->graphicsView->setSpeed(100);
}

void MainWindow::increaseSpeed()
{
    if (!ui->graphicsView->getCurrentFileDetails().isMovieLoaded)
        return;

    ui->graphicsView->setSpeed(ui->graphicsView->getLoadedMovie().speed()+25);
}

void MainWindow::toggleFullScreen()
{
    const auto fullscreenActions = qvApp->getActionManager()->getAllInstancesOfAction("fullscreen");

    if (windowState() == Qt::WindowFullScreen)
    {
        showNormal();
        for (const auto &fullscreenAction : fullscreenActions)
        {
            fullscreenAction->setText(tr("Enter Full Screen"));
            fullscreenAction->setIcon(QIcon::fromTheme("view-fullscreen"));
        }
    }
    else
    {
        showFullScreen();
        for (const auto &fullscreenAction : fullscreenActions)
        {
            fullscreenAction->setText(tr("Exit Full Screen"));
            fullscreenAction->setIcon(QIcon::fromTheme("view-restore"));
        }
    }
}

void MainWindow::quit()
{
    close();
    QCoreApplication::quit();
}
