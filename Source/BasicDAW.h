/*******************************************************************************
 The block below describes the properties of this PIP. A PIP is a short snippet
 of code that can be read by the Projucer and used to generate a JUCE project.

 BEGIN_JUCE_PIP_METADATA

  name:             BasicDAW
  version:          0.0.1
  vendor:           Deepmusic

  dependencies:     juce_audio_basics, juce_audio_devices, juce_audio_formats, juce_audio_processors, juce_audio_utils,
                    juce_core, juce_data_structures, juce_dsp, juce_events, juce_graphics,
                    juce_gui_basics, juce_gui_extra, juce_osc, tracktion_engine
  exporters:        linux_make, vs2017, xcode_iphone, xcode_mac

  moduleFlags:      JUCE_STRICT_REFCOUNTEDPOINTER=1, JUCE_PLUGINHOST_AU=1, JUCE_PLUGINHOST_VST3=1

  type:             Component
  mainClass:        BasicDAW

 END_JUCE_PIP_METADATA

*******************************************************************************/


#pragma once

#include "common/Utilities.h"
#include "common/Components.h"
#include "common/PluginWindow.h"
#include "json/json.h"

//==============================================================================
class BasicDAW  : public Component,
                  private ChangeListener
{
public:
    //==============================================================================
    BasicDAW()
    {
        settingsButton.onClick  = [this] { EngineHelpers::showAudioDeviceSettings (engine); };
        pluginsButton.onClick = [this]
        {
            DialogWindow::LaunchOptions o;
            o.dialogTitle                   = TRANS("Plugins");
            o.dialogBackgroundColour        = Colours::black;
            o.escapeKeyTriggersCloseButton  = true;
            o.useNativeTitleBar             = true;
            o.resizable                     = true;
            o.useBottomRightCornerResizer   = true;
            
            auto v = new PluginListComponent (engine.getPluginManager().pluginFormatManager,
                                              engine.getPluginManager().knownPluginList,
                                              engine.getTemporaryFileManager().getTempFile ("PluginScanDeadMansPedal"),
                                              te::getApplicationSettings());
            v->setSize (800, 600);
            o.content.setOwned (v);
            o.launchAsync();
        };
        newEditButton.onClick = [this] { createOrLoadEdit(File{}, false); };
        openEditButton.onClick = [this] { createOrLoadEdit(File{}, true); };
        saveEditButton.onClick = [this] { tracktion_engine::EditFileOperations(*edit).save(true, true, false); };
        saveAsEditButton.onClick = [this] {juce::FileChooser fc("Save As...",
                                                                juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                                "*.tracktionedit");
                                                            if (fc.browseForFileToSave(true))
                                                            {
                                                                auto editFile = fc.getResult();
                                                                tracktion_engine::EditFileOperations(*edit).saveAs(editFile);
                                                            }};
        importMidiButton.onClick = [this] {
            File file{};
            FileChooser fileChooser("Open Midi File", file, "*.mid");

            if (fileChooser.browseForFileToOpen())
            {
                file = fileChooser.getResult();
                FileInputStream stream(file);

                MidiFile midiFile{};

                if (midiFile.readFrom(stream))
                {
                    midiFile.convertTimestampTicksToSeconds();
                    juce::MidiMessageSequence sequence;

                    if (midiFile.getNumTracks() == 1) {
                        for (auto i = 0; i < midiFile.getNumTracks(); i++)
                            sequence.addSequence(*midiFile.getTrack(i), 0.0, 0.0, 1.0e6);
                        
                        sequence.updateMatchedPairs();
                        
                        auto track = getAudioTracks (*edit).getFirst();
//                        auto sel = selectionManager.getSelectedObject (0);
//                        if (auto track = dynamic_cast<te::Track*> (sel))
//                        {
                        if (! (track->isMarkerTrack() || track->isTempoTrack() || track->isChordTrack()))
                        {
                            const auto midiTrack{ dynamic_cast<te::AudioTrack*>(track) };

                            if (auto midiClip = midiTrack->insertMIDIClip({ 0.0, 1.0 }, nullptr))
                            {
                                // add midi notes
                                midiClip->mergeInMidiSequence(sequence, te::MidiList::NoteAutomationType::none);
                                auto length = sequence.getEndTime();
                                midiClip->setPosition({ { 0.0, length }, 0.0 });
                            }
                        }
//                        }
                    } else {
                        AlertWindow::showOkCancelBox(AlertWindow::InfoIcon, "Midi File Error", "Only support single track Midi");
                    }
                }
            }
        };
        
        renderButton.onClick = [this] {
            FileChooser fileChooser("Select a folder");
            if( fileChooser.browseForFileToSave(true) ) {
                File renderFile{ fileChooser.getResult().getNonexistentChildFile("render", ".wav") };
                te::EditTimeRange range{ 0.0, edit->getLength() };

                juce::BigInteger tracksToDo{ 0 };

                for (auto i = 0; i< te::getAllTracks(*edit).size(); i++)
                    tracksToDo.setBit(i);

                render.renderToFile("Render", renderFile, *edit, range, tracksToDo, true, {}, false);
            }
        };
        
        importDexedPramButton.onClick = [this] {
            auto track = getAudioTracks (*edit).getFirst();
            auto plugins = track->pluginList.getPlugins();

            auto params = plugins[0]->getAutomatableParameters();
            
            std::map<juce::String, juce::String> m;
            for (int i = 0; i < params.size(); i++) {
                m[params[i]->paramName] = params[i]->paramID;
            }
            
            File file{};
            FileChooser fileChooser("Open Params File", file, "*.json");
            if (fileChooser.browseForFileToOpen())
            {
                file = fileChooser.getResult();
                FileInputStream stream(file);
                paramString = stream.readString();
                
                nlohmann::json json_params = nlohmann::json::parse(paramString.toStdString());
                
                std::map<juce::String, float> paramsMap;
                
                for (nlohmann::json::iterator it = json_params.begin(); it != json_params.end(); ++it) {
                    paramsMap[m[it.key()]] = it.value();
                }
                for (auto const& i : paramsMap) {
                    plugins[0]->getAutomatableParameterByID(i.first)->setParameter(i.second, NotificationType::sendNotification);
                }
            }
        };
        
        batchButton.onClick = [this] {
            Array<File> files{};
            if (AlertWindow::showOkCancelBox(AlertWindow::InfoIcon, "Presets Folder", "Please select Dexed presets folder, presets shuold be .json format")) {
                FileChooser fileChooser("Select .json folder");
                if (fileChooser.browseForDirectory()){
                    files = fileChooser.getResult().findChildFiles(2, true, "*.json");
                    
                    auto track = getAudioTracks (*edit).getFirst();
                    auto plugins = track->pluginList.getPlugins();
                    auto params = plugins[0]->getAutomatableParameters();
                    std::map<juce::String, juce::String> m;
                    for (int i = 0; i < params.size(); i++) {
                        m[params[i]->paramName] = params[i]->paramID;
                    }
                    
                    if (AlertWindow::showOkCancelBox(AlertWindow::InfoIcon, "Export Folder", "Please select export directory, render files will be .wav format")) {
                        FileChooser renderChooser("Select export directory");
                        renderChooser.browseForFileToSave(true);
                        
                        for (auto const& i : files) {
                            auto name = i.getFileNameWithoutExtension();
                            FileInputStream stream(i);
                            paramString = stream.readString();
                            nlohmann::json json_params = nlohmann::json::parse(paramString.toStdString());
                            std::map<juce::String, float> paramsMap;
                            for (nlohmann::json::iterator it = json_params.begin(); it != json_params.end(); ++it) {
                                paramsMap[m[it.key()]] = it.value();
                            }
                            for (auto const& i : paramsMap) {
                                plugins[0]->getAutomatableParameterByID(i.first)->setParameter(i.second, NotificationType::sendNotification);
                            }
                            
                            File renderFile{ renderChooser.getResult().getNonexistentChildFile(name, ".wav") };
                            te::EditTimeRange range{ 0.0, edit->getLength() };
                            juce::BigInteger tracksToDo{ 0 };
                            for (auto i = 0; i< te::getAllTracks(*edit).size(); i++)
                                tracksToDo.setBit(i);
                            render.renderToFile("render", renderFile, *edit, range, tracksToDo, true, {}, false);
                            
                        }
                    } else {}
                }
            } else {}
        };
        
        updatePlayButtonText();
        updateRecordButtonText();
        
        editNameLabel.setJustificationType (Justification::centred);
        
        Helpers::addAndMakeVisible (*this, { &settingsButton, &pluginsButton, &newEditButton, &openEditButton, &saveEditButton, &saveAsEditButton, &importMidiButton, &playPauseButton, &showEditButton, &recordButton, &newTrackButton, &deleteButton, &editNameLabel, &showWaveformButton, &renderButton, &importDexedPramButton, &batchButton });

        deleteButton.setEnabled (false);
        
        auto d = File::getSpecialLocation (File::tempDirectory).getChildFile ("Host");
        d.createDirectory();
        
        auto f = Helpers::findRecentEdit (d);
        if (f.existsAsFile())
            createOrLoadEdit (f, true);
        else
            createOrLoadEdit (d.getNonexistentChildFile ("Host", ".tracktionedit", false), false);
        
        selectionManager.addChangeListener (this);
        
        setupButtons();
        
        setSize (1000, 150);
    }

    ~BasicDAW()
    {
        te::EditFileOperations (*edit).save (true, true, false);
        engine.getTemporaryFileManager().getTempDirectory().deleteRecursively();
    }

    //==============================================================================
    void paint (Graphics& g) override
    {
        g.fillAll (getLookAndFeel().findColour (ResizableWindow::backgroundColourId));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        int w = r.getWidth() / 12;
        auto topR = r.removeFromTop (30);
        settingsButton.setBounds (topR.removeFromLeft (w).reduced (2));
        pluginsButton.setBounds (topR.removeFromLeft (w).reduced (2));
//        newEditButton.setBounds (topR.removeFromLeft (w).reduced (2));
        openEditButton.setBounds (topR.removeFromLeft (w).reduced (2));
        saveEditButton.setBounds (topR.removeFromLeft (w).reduced (2));
        saveAsEditButton.setBounds (topR.removeFromLeft (w).reduced (2));
        importMidiButton.setBounds(topR.removeFromLeft (w).reduced (2));
        playPauseButton.setBounds (topR.removeFromLeft (w).reduced (2));
        recordButton.setBounds (topR.removeFromLeft (w).reduced (2));
//        showEditButton.setBounds (topR.removeFromLeft (w).reduced (2));
//        newTrackButton.setBounds (topR.removeFromLeft (w).reduced (2));
        deleteButton.setBounds (topR.removeFromLeft (w).reduced (2));
        importDexedPramButton.setBounds (topR.removeFromLeft (w).reduced (2));
        renderButton.setBounds (topR.removeFromLeft (w).reduced (2));
        batchButton.setBounds (topR.removeFromLeft (w).reduced (2));
        topR = r.removeFromTop (30);
        showWaveformButton.setBounds (topR.removeFromLeft (w * 2).reduced (2));
        editNameLabel.setBounds (topR.removeFromLeft (w * 10));
        
        if (editComponent != nullptr)
            editComponent->setBounds (r);
    }

private:
    //==============================================================================
    te::Engine engine { ProjectInfo::projectName, std::make_unique<ExtendedUIBehaviour>(), nullptr };
    te::SelectionManager selectionManager { engine };
    std::unique_ptr<te::Edit> edit;
    std::unique_ptr<EditComponent> editComponent;
    te::Renderer render;

    TextButton settingsButton { "Settings" }, pluginsButton { "Plugins" }, newEditButton { "New" }, openEditButton { "Open" }, saveEditButton { "Save" }, saveAsEditButton { "Save as" }, playPauseButton { "Play" },
    showEditButton { "Show Edit" }, newTrackButton { "New Track" }, deleteButton { "Delete" }, recordButton { "Record" }, importMidiButton{ "Import Midi" }, renderButton{ "Render" }, importDexedPramButton { "Dexed Params" }, batchButton{ "Batch" };
    Label editNameLabel { "No Edit Loaded" };
    ToggleButton showWaveformButton { "Show Waveforms" };
    
    File jsonFile;
    
    String paramString;

    //==============================================================================
    void setupButtons()
    {
        playPauseButton.onClick = [this]
        {
            EngineHelpers::togglePlay (*edit);
        };
        recordButton.onClick = [this]
        {
            bool wasRecording = edit->getTransport().isRecording();
            EngineHelpers::toggleRecord (*edit);
            if (wasRecording)
                te::EditFileOperations (*edit).save (true, true, false);
        };
        newTrackButton.onClick = [this]
        {
            edit->ensureNumberOfAudioTracks (getAudioTracks (*edit).size() + 1);
        };
        deleteButton.onClick = [this]
        {
            auto sel = selectionManager.getSelectedObject (0);
            if (auto clip = dynamic_cast<te::Clip*> (sel))
            {
                clip->removeFromParentTrack();
            }
//            else if (auto track = dynamic_cast<te::Track*> (sel))
//            {
//                if (! (track->isMarkerTrack() || track->isTempoTrack() || track->isChordTrack()))
//                    edit->deleteTrack (track);
//            }
            else if (auto plugin = dynamic_cast<te::Plugin*> (sel))
            {
                plugin->deleteFromParent();
            }
        };
        showWaveformButton.onClick = [this]
        {
            auto& evs = editComponent->getEditViewState();
            evs.drawWaveforms = ! evs.drawWaveforms.get();
            showWaveformButton.setToggleState (evs.drawWaveforms, dontSendNotification);
        };
    }
    
    void updatePlayButtonText()
    {
        if (edit != nullptr)
            playPauseButton.setButtonText (edit->getTransport().isPlaying() ? "Stop" : "Play");
    }
    
    void updateRecordButtonText()
    {
        if (edit != nullptr)
            recordButton.setButtonText (edit->getTransport().isRecording() ? "Abort" : "Record");
    }
    
    void createOrLoadEdit (File editFile ,bool loadOnly)
    {
        
        if (editFile == juce::File())
        {
            auto title = juce::String(loadOnly ? "Load" : "New") + " Project";
            juce::FileChooser fc(title, juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.tracktionedit");
            auto result = loadOnly ? fc.browseForFileToOpen(): fc.browseForFileToSave(false);
            if (result)
                editFile = fc.getResult();
            else
                return;
        }
        
        selectionManager.deselectAll();
        editComponent = nullptr;
        
        if (editFile.existsAsFile())
            edit = te::loadEditFromFile (engine, editFile);
        else
            edit = te::createEmptyEdit (engine, editFile);

        edit->editFileRetriever = [editFile] { return editFile; };
        edit->playInStopEnabled = true;
        
        auto& transport = edit->getTransport();
        transport.addChangeListener (this);
        
        editNameLabel.setText (editFile.getFileNameWithoutExtension(), dontSendNotification);
        showEditButton.onClick = [this, editFile]
        {
            te::EditFileOperations (*edit).save (true, true, false);
            editFile.revealToUser();
        };
        
        createTracksAndAssignInputs();
        
        te::EditFileOperations (*edit).save (true, true, false);
        
        editComponent = std::make_unique<EditComponent> (*edit, selectionManager);
        editComponent->getEditViewState().showFooters = true;
        editComponent->getEditViewState().showMidiDevices = true;
        editComponent->getEditViewState().showWaveDevices = true;
        
        addAndMakeVisible (*editComponent);
        resized();
    }
    
    void changeListenerCallback (ChangeBroadcaster* source) override
    {
        if (edit != nullptr && source == &edit->getTransport())
        {
            updatePlayButtonText();
            updateRecordButtonText();
        }
        else if (source == &selectionManager)
        {
            auto sel = selectionManager.getSelectedObject (0);
            deleteButton.setEnabled (dynamic_cast<te::Clip*> (sel) != nullptr
                                     || dynamic_cast<te::Track*> (sel) != nullptr
                                     || dynamic_cast<te::Plugin*> (sel));
        }
    }
    
    void createTracksAndAssignInputs()
    {
        auto& dm = engine.getDeviceManager();
        
        for (int i = 0; i < dm.getNumMidiInDevices(); i++)
        {
            if (auto mip = dm.getMidiInDevice (i))
            {
                mip->setEndToEndEnabled (true);
                mip->setEnabled (true);
            }
        }
        
        edit->getTransport().ensureContextAllocated();
        
        if (te::getAudioTracks (*edit).size () == 0)
        {
            int trackNum = 0;
            for (auto instance : edit->getAllInputDevices())
            {
                if (instance->getInputDevice().getDeviceType() == te::InputDevice::physicalMidiDevice)
                {
                    if (auto t = EngineHelpers::getOrInsertAudioTrackAt (*edit, trackNum))
                    {
                        instance->setTargetTrack (*t, 0, true);
                        instance->setRecordingEnabled (*t, true);
                    
                        trackNum++;
                    }
                }
            }
        }
        
        edit->restartPlayback();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BasicDAW)
};

