/*
 * Copyright (c) 2015-2020 CE Programming.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "corewindow.h"

#include "calculatorwidget.h"
#include "capturewidget.h"
#include "consolewidget.h"
#include "dockwidget.h"
#include "keyhistorywidget.h"
#include "romdialog.h"
#include "settings.h"
#include "settingsdialog.h"
#include "statewidget.h"
#include "util.h"
#include "variablewidget.h"

#include "developer/autotesterwidget.h"
#include "developer/breakpointswidget.h"
#include "developer/clockswidget.h"
#include "developer/controlwidget.h"
#include "developer/cpuwidget.h"
#include "developer/devmiscwidget.h"
#include "developer/disassemblywidget.h"
#include "developer/memorywidget.h"
#include "developer/osstackswidget.h"
#include "developer/osvarswidget.h"
#include "developer/portmonitorwidget.h"
#include "developer/watchpointswidget.h"
#include "developer/visualizerwidget.h"

#include "keypad/qtkeypadbridge.h"

#include <kddockwidgets/LayoutSaver.h>

#include <QtCore/QDebug>
#include <QtCore/QEvent>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QTextEdit>

const QString CoreWindow::sErrorStr        = CoreWindow::tr("Error");
const QString CoreWindow::sWarningStr      = CoreWindow::tr("Warning");
const QString CoreWindow::sInformationStr  = CoreWindow::tr("Information");

CoreWindow::CoreWindow(const QString &uniqueName,
                       KDDockWidgets::MainWindowOptions options,
                       QWidget *parent)
    : KDDockWidgets::MainWindow(uniqueName, options, parent),
      mKeypadBridge{new QtKeypadBridge{this}},
      mCalcOverlay{nullptr},
      mCalcType{ti_device_t::TI84PCE}
{
    auto *menubar = menuBar();

    mCalcsMenu = new QMenu(tr("Calculator"), this);
    mDocksMenu = new QMenu(tr("Docks"), this);
    mDevMenu = new QMenu(tr("Developer"), this);

    menubar->addMenu(mCalcsMenu);
    menubar->addMenu(mDocksMenu);
    menubar->addMenu(mDevMenu);

    auto *resetAction = mCalcsMenu->addAction(tr("Reset"));
    connect(resetAction, &QAction::triggered, this, &CoreWindow::resetEmu);

    auto *romAction = mCalcsMenu->addAction(tr("Load ROM..."));
    connect(romAction, &QAction::triggered, this, &CoreWindow::loadRom);

    mCalcsMenu->addSeparator();

    auto *prefAction = mCalcsMenu->addAction(tr("Preferences"));
    connect(prefAction, &QAction::triggered, this, &CoreWindow::showPreferences);

    auto *quitAction = mCalcsMenu->addAction(tr("Quit"));
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    createDockWidgets();

    auto *saveLayoutAction = mDocksMenu->addAction(tr("Save Layout"));
    connect(saveLayoutAction, &QAction::triggered, this, &CoreWindow::saveLayout);

    auto *restoreLayoutAction = mDocksMenu->addAction(tr("Restore Layout"));
    connect(restoreLayoutAction, &QAction::triggered, this, &CoreWindow::restoreLayout);

    setKeymap();

    if (!restoreLayout())
    {
        qDebug() << "Failed to restore layout";
        QRect screenGeometry = QGuiApplication::primaryScreen()->geometry();
        resize(screenGeometry.height() * .325, screenGeometry.height() * .8);
    }

    connect(this, &CoreWindow::romChanged, this, &CoreWindow::resetEmu);

    resetEmu();
}

CoreWindow::~CoreWindow()
{
    qDeleteAll(mDockWidgets);
    while (!mVisualizerWidgets.empty())
    {
        delete static_cast<VisualizerWidget *>(mVisualizerWidgets.prev())->parent();
    }
}

void CoreWindow::createDockWidgets()
{
    Q_ASSERT(mDockWidgets.isEmpty());

    auto *calcDock = new KDDockWidgets::DockWidget(tr("Calculator"));
    mCalcWidget = new CalculatorWidget();

    auto *captureDock = new KDDockWidgets::DockWidget(tr("Screen Capture"));
    auto *capture = new CaptureWidget();

    auto *variableDock = new KDDockWidgets::DockWidget(tr("Variable Transfer"));
    auto *variable = new VariableWidget(QStringList({"test", "test2"}));

    auto *keyHistoryDock = new KDDockWidgets::DockWidget(tr("Key History"));
    auto *keyHistory = new KeyHistoryWidget();

    auto *stateDock = new KDDockWidgets::DockWidget(tr("States"));
    auto *state = new StateWidget();

    mCalcOverlay = new CalculatorOverlay(mCalcWidget);
    mCalcOverlay->setVisible(false);

    connect(mCalcOverlay, &CalculatorOverlay::createRom, this, &CoreWindow::createRom);
    connect(mCalcOverlay, &CalculatorOverlay::loadRom, this, &CoreWindow::loadRom);

    calcDock->setWidget(mCalcWidget);
    keyHistoryDock->setWidget(keyHistory);
    stateDock->setWidget(state);
    captureDock->setWidget(capture);
    variableDock->setWidget(variable);

    connect(mCalcWidget, &CalculatorWidget::keyPressed, keyHistory, &KeyHistoryWidget::add);
    connect(mKeypadBridge, &QtKeypadBridge::keyStateChanged, mCalcWidget, &CalculatorWidget::changeKeyState);
    mCalcWidget->installEventFilter(mKeypadBridge);

    mDockWidgets << calcDock;
    mDockWidgets << keyHistoryDock;
    mDockWidgets << captureDock;
    mDockWidgets << stateDock;
    mDockWidgets << variableDock;

    mDocksMenu->addAction(calcDock->toggleAction());
    mDocksMenu->addAction(captureDock->toggleAction());
    mDocksMenu->addAction(keyHistoryDock->toggleAction());
    mDocksMenu->addAction(variableDock->toggleAction());
    mDocksMenu->addAction(stateDock->toggleAction());

    addDockWidget(calcDock, KDDockWidgets::Location_OnTop);

    createDeveloperWidgets();
}

void CoreWindow::createDeveloperWidgets()
{
    QList<Breakpoint> breakpoints =
    {
        {true, 10, "test"},
        {false, 20, "test2"},
        {true, 30, "test3"}
    };
    QList<Watchpoint> watchpoints =
    {
        {true, Watchpoint::Mode::Read, 10, 5, "test"},
        {true, Watchpoint::Mode::Write, 20, 15, "test2"},
        {false, Watchpoint::Mode::ReadWrite, 30, 25, "test3"},
        {true, Watchpoint::Mode::Write, 40, 35, "test4"}
    };

    auto *consoleDock = new KDDockWidgets::DockWidget(tr("Console"));
    auto *console = new ConsoleWidget();

    auto *autotesterDock = new KDDockWidgets::DockWidget(tr("Autotester"));
    auto *autotester = new AutotesterWidget();

    auto *breakpointsDock = new KDDockWidgets::DockWidget(tr("Breakpoints"));
    auto *mBreakpointWidget = new BreakpointsWidget(breakpoints);

    auto *clocksDock = new KDDockWidgets::DockWidget(tr("Clocks"));
    auto *clocks = new ClocksWidget();

    auto *controlDock = new KDDockWidgets::DockWidget(tr("Control"));
    auto *control = new ControlWidget();

    auto *cpuDock = new KDDockWidgets::DockWidget(tr("CPU"));
    auto *cpu = new CpuWidget();

    auto *devMiscDock = new KDDockWidgets::DockWidget(tr("Miscellaneous"));
    auto *devMisc = new DevMiscWidget();

    auto *dissassmeblyDock = new KDDockWidgets::DockWidget(tr("Disassembly"));
    auto *dissassmebly = new DisassemblyWidget();

    auto *memoryDock = new KDDockWidgets::DockWidget(tr("Memory"));
    auto *memory = new MemoryWidget();

    auto *osStacksDock = new KDDockWidgets::DockWidget(tr("OS Stacks"));
    auto *osStacks = new OsStacksWidget();

    auto *osVarsDock = new KDDockWidgets::DockWidget(tr("OS Variables"));
    auto *osVars = new OsVarsWidget();

    auto *portMonitorDock = new KDDockWidgets::DockWidget(tr("Port Monitor"));
    auto *portMonitor = new PortMonitorWidget();

    auto *watchpointsDock = new KDDockWidgets::DockWidget(tr("Watchpoints"));
    auto *mWatchpointWidget = new WatchpointsWidget(watchpoints);

    autotesterDock->setWidget(autotester);
    breakpointsDock->setWidget(mBreakpointWidget);
    clocksDock->setWidget(clocks);
    consoleDock->setWidget(console);
    controlDock->setWidget(control);
    cpuDock->setWidget(cpu);
    devMiscDock->setWidget(devMisc);
    dissassmeblyDock->setWidget(dissassmebly);
    memoryDock->setWidget(memory);
    osStacksDock->setWidget(osStacks);
    osVarsDock->setWidget(osVars);
    portMonitorDock->setWidget(portMonitor);
    watchpointsDock->setWidget(mWatchpointWidget);

    mDockWidgets << autotesterDock;
    mDockWidgets << breakpointsDock;
    mDockWidgets << clocksDock;
    mDockWidgets << consoleDock;
    mDockWidgets << controlDock;
    mDockWidgets << cpuDock;
    mDockWidgets << devMiscDock;
    mDockWidgets << dissassmeblyDock;
    mDockWidgets << memoryDock;
    mDockWidgets << osStacksDock;
    mDockWidgets << osVarsDock;
    mDockWidgets << portMonitorDock;
    mDockWidgets << watchpointsDock;

    mDevMenu->addAction(consoleDock->toggleAction());
    mDevMenu->addAction(controlDock->toggleAction());
    mDevMenu->addAction(cpuDock->toggleAction());
    mDevMenu->addAction(dissassmeblyDock->toggleAction());
    mDevMenu->addAction(memoryDock->toggleAction());
    mDevMenu->addAction(clocksDock->toggleAction());
    mDevMenu->addAction(breakpointsDock->toggleAction());
    mDevMenu->addAction(watchpointsDock->toggleAction());
    mDevMenu->addAction(portMonitorDock->toggleAction());
    mDevMenu->addAction(osVarsDock->toggleAction());
    mDevMenu->addAction(osStacksDock->toggleAction());
    mDevMenu->addAction(devMiscDock->toggleAction());
    mDevMenu->addAction(autotesterDock->toggleAction());

    mDevMenu->addSeparator();

    auto *memoryAction = mDevMenu->addAction(tr("Add Memory View"));
    connect(memoryAction, &QAction::triggered, this, &CoreWindow::restoreLayout);

    auto *visualizerAction = mDevMenu->addAction(tr("Add LCD Visualizer"));
    connect(visualizerAction, &QAction::triggered, [this]
    {
        QString name;
        do
        {
            name = QLatin1String("Visualizer #") + Util::randomString(10);
        }
        while (KDDockWidgets::DockWidgetBase::byName(name));

        auto *dockWidget = new KDDockWidgets::DockWidget(name);
        addVisualizerDock(dockWidget);
        dockWidget->show();
    });
}

void CoreWindow::setKeymap()
{
    Keymap keymap = static_cast<Keymap>(Settings::intOption(Settings::KeyMap));
    mKeypadBridge->setKeymap(keymap);
}

void CoreWindow::createRom()
{
    RomDialog romdialog;
    if (romdialog.exec())
    {
        QString romFile = romdialog.romFile();

        Settings::setTextOption(Settings::RomFile, romFile);
        emit romChanged();
    }
}

void CoreWindow::loadRom()
{
    QFileDialog dialog(this);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setWindowTitle(tr("Select ROM file"));
    dialog.setNameFilter(tr("ROM Image (*.rom *.Rom *.ROM);;All Files (*.*)"));
    if (dialog.exec())
    {
        QString romFile = dialog.selectedFiles().first();

        Settings::setTextOption(Settings::RomFile, romFile);
        emit romChanged();
    }
}

void CoreWindow::resetEmu()
{
    int keycolor = Settings::intOption(Settings::KeypadColor);

    // holds the path to the rom file to load into the emulator
    //Settings::textOption(Settings::RomFile);
    static bool test = false;

    if (test)
    {

        mCalcWidget->setConfig(mCalcType, keycolor);
        mCalcOverlay->setVisible(false);
    }
    else
    {
        mCalcWidget->setConfig(mCalcType, keycolor);
        mCalcOverlay->setVisible(true);
    }

    test = true;
}

void CoreWindow::showPreferences()
{
    SettingsDialog dialog;
    connect(&dialog, &SettingsDialog::changedKeypadColor, [this](int color)
    {
        mCalcWidget->setConfig(mCalcType, color);
    });
    dialog.exec();
}

bool CoreWindow::saveLayout(bool ignoreErrors)
{
    KDDockWidgets::LayoutSaver saver;
    QJsonObject json;
    json[QLatin1String("layout")] = QJsonDocument::fromJson(saver.serializeLayout()).object();
    for (auto *visualizerWidgetNode = mVisualizerWidgets.next();
         visualizerWidgetNode != &mVisualizerWidgets;
         visualizerWidgetNode = visualizerWidgetNode->next())
    {
        auto *visualizerWidget = static_cast<VisualizerWidget *>(visualizerWidgetNode);
        auto *dockWidget = static_cast<KDDockWidgets::DockWidget *>(visualizerWidget->parent());
        json[dockWidget->uniqueName()] = visualizerWidget->getConfig();
    }

    QFile file(Settings::textOption(Settings::LayoutFile));
    if (!file.open(QIODevice::WriteOnly))
    {
        if (!ignoreErrors)
        {
            QMessageBox::critical(nullptr, sErrorStr, tr("Unable to save layout. Ensure that the preferences directory is writable and has the required permissions."));
        }
        return false;
    }
    file.write(QJsonDocument(json).toJson());
    return true;
}

bool CoreWindow::restoreLayout()
{
    QFile file(Settings::textOption(Settings::LayoutFile));
    if (!file.open(QIODevice::ReadOnly))
    {
        return false;
    }
    auto json = QJsonDocument::fromJson(file.readAll()).object();

    KDDockWidgets::RestoreOptions options = KDDockWidgets::RestoreOption_None;
    KDDockWidgets::LayoutSaver saver(options);
    if (!saver.restoreLayout(QJsonDocument(json.take(QLatin1String("layout")).toObject()).toJson()))
    {
        return false;
    }

    for (auto *restoredDockWidget : saver.restoredDockWidgets())
    {
        auto name = restoredDockWidget->uniqueName();
        auto config = json.take(name);
        if (name.startsWith(QLatin1String("Visualizer #")))
        {
            addVisualizerDock(restoredDockWidget, config.toString());
        }
    }

    return json.isEmpty();
}

void CoreWindow::closeEvent(QCloseEvent *)
{
    saveLayout(true);
}

void CoreWindow::addVisualizerDock(KDDockWidgets::DockWidgetBase *dockWidget, const QString &config)
{
    if (auto *visualizerWidget = dockWidget->widget())
    {
        static_cast<VisualizerWidget *>(visualizerWidget)->setConfig(config);
    }
    else
    {
        dockWidget->setTitle(tr("Visualizer"));
        dockWidget->setWidget(new VisualizerWidget(&mVisualizerWidgets, config, dockWidget));
    }
}
