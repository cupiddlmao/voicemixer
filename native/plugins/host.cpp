// Plugin code and editors live in this process, never in the mixer controller.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#include <spa/param/latency-utils.h>
#include <atomic>
#include <mutex>
#include <iostream>
#include <cmath>
#include <unistd.h>
#include "wider_controls.h"
#include "autotune_controls.h"
using namespace juce;
namespace {
constexpr int maxFrames=8192;
struct Slot { PluginDescription description;std::unique_ptr<AudioPluginInstance> plugin;bool bypass=false;AudioBuffer<float> audio; };
using voicemixer::WiderControls;
struct Editor : DocumentWindow {
    bool deferAutosave=false;
    Editor(AudioPluginInstance& p,bool generic=false,bool original=false):DocumentWindow(p.getName()+(generic?" - Parameters":""),Colour(0xff344a57),DocumentWindow::closeButton){setUsingNativeTitleBar(true);AudioProcessorEditor* content=nullptr;
        if(!generic&&!original&&p.getName().equalsIgnoreCase("Wider"))content=new WiderControls(p);
        else if(!generic&&!original&&p.getName().equalsIgnoreCase("Auto-Tune Pro"))content=new voicemixer::AutoTuneControls(p);
        else content=!generic&&p.hasEditor()?p.createEditorIfNeeded():new GenericAudioProcessorEditor(p);
        deferAutosave=!generic&&(original||(!p.getName().equalsIgnoreCase("Wider")&&!p.getName().equalsIgnoreCase("Auto-Tune Pro")));
        setContentOwned(content,true);centreWithSize(getWidth(),getHeight());setVisible(true);}
    void closeButtonPressed() override{setVisible(false);}
};
class Rack final : public Component,private Timer,private AudioProcessorListener {
public:
    File folder;String nodeName,title;AudioPluginFormatManager formats;
    std::vector<Slot> slots;std::vector<std::unique_ptr<Editor>> editors;
    std::mutex mutex;MidiBuffer midi;AudioBuffer<float> storage{2,maxFrames};
    pw_thread_loop* loop=nullptr;pw_filter* filter=nullptr;void* ports[4]{};
    std::atomic<double> requestedRate{48000},rate{48000};
    std::atomic<bool> failed{false},streaming{false},stateDirty{true};
    std::atomic<uint64_t> callbacks{0};uint64_t lastCallbacks=0,heartbeatSequence=0;int stalledTicks=0;
    TextButton hideButton{"Hide rack"};
    TextButton add{"Add VST3 / LADSPA"},remove{"Remove"},edit{"Open editor"},parameters{"Parameters"},original{"Plugin UI"},bypass{"Bypass"},up{"Move up"},down{"Move down"};
    ComboBox list;Label status;std::unique_ptr<FileChooser> chooser;int ticks=0,operationDepth=0;bool saving=false,editorWasOpen=false,incompleteLoad=false;
    explicit Rack(File dir,String node,String label):folder(dir),nodeName(node),title(label){
        folder.createDirectory();formats.addDefaultFormats();
        publishVisibility(false);
        for(auto* b:{&add,&remove,&edit,&parameters,&original,&bypass,&up,&down})addAndMakeVisible(b);
        addAndMakeVisible(hideButton);hideButton.onClick=[this]{if(hide)hide();};
        addAndMakeVisible(list);addAndMakeVisible(status);status.setText("Stereo insert - native VST3 / LADSPA - Windows VST3 via yabridge",dontSendNotification);
        add.onClick=[this]{choose();};remove.onClick=[this]{change(0);};bypass.onClick=[this]{change(1);};up.onClick=[this]{change(2);};down.onClick=[this]{change(3);};edit.onClick=[this]{openEditor();};parameters.onClick=[this]{openEditor(true);};original.onClick=[this]{openEditor(false,true);};
        load();refreshList();setSize(850,190);startAudio();startTimer(250);
    }
    ~Rack() override{stopTimer();if(loop){pw_thread_loop_stop(loop);if(filter)pw_filter_destroy(filter);pw_thread_loop_destroy(loop);}save();editors.clear();for(auto& s:slots){s.plugin->removeListener(this);s.plugin->releaseResources();}}
    void paint(Graphics& g) override{g.fillAll(Colour(0xff344a57));g.setColour(Colour(0xff8be1bd));g.setFont(18);g.drawText(title+" - Plugin rack",12,8,getWidth()-134,26,Justification::centredLeft);}
    bool keyPressed(const KeyPress& key) override{if(key==KeyPress::escapeKey){if(hide)hide();return true;}return false;}
    void resized() override{hideButton.setBounds(getWidth()-112,8,100,26);list.setBounds(12,45,getWidth()-24,28);int x=12;for(auto* b:{&add,&edit,&parameters,&original,&bypass,&remove,&up,&down}){int w=b==&add?155:b==&edit?100:83;b->setBounds(x,85,w,28);x+=w+5;}status.setBounds(12,125,getWidth()-24,50);}
    void report(String s){status.setText(s,dontSendNotification);std::cerr<<s<<'\n';}
    void publishVisibility(bool visible){
        auto* info=new DynamicObject;info->setProperty("pid",int64(getpid()));info->setProperty("visible",visible);
        TemporaryFile tmp(folder.getChildFile("visibility.json"));if(tmp.getFile().replaceWithText(JSON::toString(var(info))))tmp.overwriteTargetFileWithTemporary();
    }
    bool instantiate(const PluginDescription& d,const String& state,bool disabled){
        Operation operation(*this,"load "+d.name);
        String error;auto p=formats.createPluginInstance(d,rate,maxFrames,error);
        if(!p){report("Could not load "+d.name+": "+error);return false;}
        p->enableAllBuses();auto layout=p->getBusesLayout();
        if(layout.inputBuses.isEmpty()||layout.outputBuses.isEmpty()){report(d.name+": only audio effects with input and output buses are supported");return false;}
        // Keep auxiliary buses allocated: some bridged plugins access their
        // buffers even when the bus was disabled. Unconnected inputs get silence.
        layout.inputBuses.set(0,AudioChannelSet::stereo());layout.outputBuses.set(0,AudioChannelSet::stereo());
        if(!p->setBusesLayout(layout)){report(d.name+": this effect does not support stereo input/output");return false;}
        if(state.isNotEmpty()){MemoryBlock data;if(data.fromBase64Encoding(state))p->setStateInformation(data.getData(),int(data.getSize()));}
        p->setRateAndBufferSizeDetails(rate,maxFrames);p->prepareToPlay(rate,maxFrames);p->addListener(this);stateDirty=true;
        const int channels=jmax(2,p->getTotalNumInputChannels(),p->getTotalNumOutputChannels());
        if(channels>16){p->removeListener(this);p->releaseResources();report(d.name+": more than 16 internal channels are not supported");return false;}
        std::lock_guard<std::mutex> guard(mutex);slots.push_back({d,std::move(p),disabled,AudioBuffer<float>(channels,maxFrames)});return true;
    }
    void load(){
        auto doc=JSON::parse(folder.getChildFile("rack.json"));const auto items=doc["plugins"];
        const String quarantined=folder.getChildFile("loading.txt").loadFileAsString();
        if(auto* a=items.getArray())for(const auto& item:*a){
            auto xml=parseXML(item["description"].toString());PluginDescription d;if(!xml||!d.loadFromXml(*xml)){incompleteLoad=true;continue;}
            if(d.createIdentifierString()==quarantined){incompleteLoad=true;report("Skipped a plugin that previously crashed while loading: "+d.name);continue;}
            folder.getChildFile("loading.txt").replaceWithText(d.createIdentifierString());
            if(!instantiate(d,item["state"].toString(),bool(item["bypass"])))incompleteLoad=true;
            folder.getChildFile("loading.txt").deleteFile();
        }
    }
    void pulse(){
        auto* info=new DynamicObject;info->setProperty("pid",int64(getpid()));info->setProperty("sequence",int64(++heartbeatSequence));
        TemporaryFile tmp(folder.getChildFile("heartbeat.json"));if(tmp.getFile().replaceWithText(JSON::toString(var(info))))tmp.overwriteTargetFileWithTemporary();
    }
    struct Operation {
        Rack& rack;String phase;double started=Time::getMillisecondCounterHiRes();
        Operation(Rack& r,String label):rack(r),phase(std::move(label)){
            if(rack.operationDepth++==0){
                auto* info=new DynamicObject;info->setProperty("pid",int64(getpid()));info->setProperty("phase",phase);
                TemporaryFile tmp(rack.folder.getChildFile("busy.json"));if(tmp.getFile().replaceWithText(JSON::toString(var(info))))tmp.overwriteTargetFileWithTemporary();
            }
            std::cerr<<"Rack operation begin: "<<phase<<std::endl;
        }
        ~Operation(){
            std::cerr<<"Rack operation end: "<<phase<<" ("<<int(Time::getMillisecondCounterHiRes()-started)<<" ms)"<<std::endl;
            if(--rack.operationDepth==0){rack.pulse();rack.folder.getChildFile("busy.json").deleteFile();}
        }
    };
    void audioProcessorParameterChanged(AudioProcessor*,int,float) override{stateDirty=true;}
    void audioProcessorChanged(AudioProcessor*,const AudioProcessorListener::ChangeDetails&) override{stateDirty=true;}
    bool save(){
        if(saving||incompleteLoad)return false;ScopedValueSetter<bool> savingGuard(saving,true);
        Operation operation(*this,"save state");stateDirty=false;
        // Slot edits and saving both run on the message thread. Do not hold
        // the rack/audio lock while invoking plugin state APIs: a bridged
        // plugin may need its audio thread to complete the state request.
        Array<var> a;
        for(auto& s:slots){MemoryBlock data;s.plugin->getStateInformation(data);auto* o=new DynamicObject;o->setProperty("description",s.description.createXml()->toString());o->setProperty("state",data.toBase64Encoding());o->setProperty("bypass",s.bypass);a.add(var(o));}
        auto* root=new DynamicObject;root->setProperty("plugins",a);const String text=JSON::toString(var(root));if(folder.getChildFile("rack.json").loadFileAsString()==text)return true;TemporaryFile tmp(folder.getChildFile("rack.json"));const bool ok=tmp.getFile().replaceWithText(text)&&tmp.overwriteTargetFileWithTemporary();if(!ok)stateDirty=true;return ok;
    }
    void choose(){
        chooser=std::make_unique<FileChooser>("Select a Linux VST3 bundle or yabridge VST3 wrapper",File::getSpecialLocation(File::userHomeDirectory).getChildFile(".vst3"),"*.vst3;*.so",false);
        chooser->launchAsync(FileBrowserComponent::openMode|FileBrowserComponent::canSelectFiles|FileBrowserComponent::canSelectDirectories,[this](const FileChooser& f){
            auto file=f.getResult();if(!file.exists()||saving)return;
            Operation operation(*this,"scan plugin");
            OwnedArray<PluginDescription> found;for(int i=0;i<formats.getNumFormats();++i)formats.getFormat(i)->findAllTypesForFile(found,file.getFullPathName());
            if(found.isEmpty()){report("No supported Linux plugin found. Windows .dll/.vst3 files need yabridge first.");return;}
            if(found.size()>1){report("This bundle contains multiple effects; the first audio effect will be loaded.");}
            // Save the existing chain before any third-party code is executed.
            save();folder.getChildFile("loading.txt").replaceWithText(found[0]->createIdentifierString());
            if(instantiate(*found[0],{},false)){save();publishLatency();refreshList();list.setSelectedId(int(slots.size()));report("Added "+found[0]->name);}
            folder.getChildFile("loading.txt").deleteFile();
        });
    }
    void refreshList(){int selected=list.getSelectedId();list.clear(dontSendNotification);int id=1;for(const auto& s:slots)list.addItem(String(id)+". "+s.description.name+(s.bypass?" [bypassed]":""),id),++id;list.setSelectedId(jlimit(1,jmax(1,int(slots.size())),selected),dontSendNotification);list.setTextWhenNothingSelected("Empty rack - audio passes through");}
    void change(int action){
        if(saving)return;
        const int i=list.getSelectedId()-1;if(i<0||i>=int(slots.size()))return;editors.clear();
        {std::lock_guard<std::mutex> guard(mutex);if(action==0){slots[i].plugin->removeListener(this);slots[i].plugin->releaseResources();slots.erase(slots.begin()+i);}else if(action==1)slots[i].bypass=!slots[i].bypass;else if(action==2&&i>0)std::swap(slots[i],slots[i-1]);else if(action==3&&i+1<int(slots.size()))std::swap(slots[i],slots[i+1]);}
        save();refreshList();publishLatency();
    }
    void openEditor(bool generic=false,bool original=false){if(saving)return;Operation operation(*this,"open editor");const int i=list.getSelectedId()-1;if(i<0||i>=int(slots.size()))return;editors.clear();editors.push_back(std::make_unique<Editor>(*slots[i].plugin,generic,original));}
    void publishLatency(){
        if(!filter)return;int samples=0;for(auto& s:slots)if(!s.bypass)samples+=s.plugin->getLatencySamples();
        uint8_t data[256];spa_pod_builder builder=SPA_POD_BUILDER_INIT(data,sizeof(data));spa_process_latency_info info{};info.rate=samples;
        const spa_pod* param=spa_process_latency_build(&builder,SPA_PARAM_ProcessLatency,&info);
        pw_thread_loop_lock(loop);pw_filter_update_params(filter,nullptr,&param,1);pw_thread_loop_unlock(loop);
    }
    static void process(void* data,spa_io_position* pos){
        auto& self=*static_cast<Rack*>(data);self.callbacks.fetch_add(1,std::memory_order_relaxed);if(!pos||!pos->clock.rate.num)return;const auto n=pos->clock.duration;
        float* in[2];float* out[2];for(int c=0;c<2;++c){in[c]=static_cast<float*>(pw_filter_get_dsp_buffer(self.ports[c],n));out[c]=static_cast<float*>(pw_filter_get_dsp_buffer(self.ports[c+2],n));}
        if(!out[0]||!out[1])return;
        for(int c=0;c<2;++c){if(in[c])std::memcpy(out[c],in[c],n*sizeof(float));else std::memset(out[c],0,n*sizeof(float));}
        const double rate=pos->clock.rate.denom/double(pos->clock.rate.num);self.requestedRate=rate;
        if(n>maxFrames||rate!=self.rate)return;
        std::unique_lock<std::mutex> guard(self.mutex,std::try_to_lock);if(!guard.owns_lock())return;
        AudioBuffer<float> block(out,2,int(n));ScopedNoDenormals noDenormals;self.midi.clear();
        for(auto& s:self.slots)if(!s.bypass){
            const int channels=s.audio.getNumChannels();
            if(channels==2){s.plugin->processBlock(block,self.midi);continue;}
            s.audio.clear(0,int(n));
            for(int c=0;c<2;++c)s.audio.copyFrom(c,0,out[c],int(n));
            AudioBuffer<float> expanded(s.audio.getArrayOfWritePointers(),channels,int(n));
            s.plugin->processBlock(expanded,self.midi);
            for(int c=0;c<2;++c)std::memcpy(out[c],s.audio.getReadPointer(c),n*sizeof(float));
        }
        // Plugins may produce invalid samples or excessive output. Retain the
        // existing bus limiter as the final stage, and stop NaNs here.
        for(int c=0;c<2;++c)for(uint32_t i=0;i<n;++i)if(!std::isfinite(out[c][i]))out[c][i]=0;
    }
    void startAudio(){
        pw_init(nullptr,nullptr);loop=pw_thread_loop_new("mixer-plugins",nullptr);
        static const pw_filter_events events=[](){pw_filter_events e{};e.version=PW_VERSION_FILTER_EVENTS;e.process=&Rack::process;e.state_changed=[](void* d,pw_filter_state,pw_filter_state s,const char*){auto& rack=*static_cast<Rack*>(d);rack.streaming=s==PW_FILTER_STATE_STREAMING;if(s==PW_FILTER_STATE_ERROR)rack.failed=true;};return e;}();
        filter=pw_filter_new_simple(pw_thread_loop_get_loop(loop),title.toRawUTF8(),pw_properties_new(PW_KEY_NODE_NAME,nodeName.toRawUTF8(),PW_KEY_NODE_DESCRIPTION,title.toRawUTF8(),PW_KEY_MEDIA_TYPE,"Audio",PW_KEY_MEDIA_CATEGORY,"Filter",PW_KEY_MEDIA_ROLE,"DSP",PW_KEY_MEDIA_CLASS,"Audio/Filter",PW_KEY_NODE_AUTOCONNECT,"false",PW_KEY_NODE_RATE,"1/48000",nullptr),&events,this);
        for(int i=0;i<4;++i)ports[i]=pw_filter_add_port(filter,i<2?PW_DIRECTION_INPUT:PW_DIRECTION_OUTPUT,PW_FILTER_PORT_FLAG_MAP_BUFFERS,1,pw_properties_new(PW_KEY_FORMAT_DSP,"32 bit float mono audio",PW_KEY_PORT_NAME,i==0?"input_FL":i==1?"input_FR":i==2?"output_FL":"output_FR",PW_KEY_AUDIO_CHANNEL,i%2==0?"FL":"FR",nullptr),nullptr,0);
        if(pw_filter_connect(filter,PW_FILTER_FLAG_RT_PROCESS,nullptr,0)<0||pw_thread_loop_start(loop)<0)failed=true;
        publishLatency();
    }
    std::function<void()> show,hide;
    void timerCallback() override{
        if(failed){report("PipeWire connection failed; close and reopen the rack");stopTimer();JUCEApplication::quit();return;}
        if(folder.getChildFile("hide.request").existsAsFile()){folder.getChildFile("hide.request").deleteFile();if(hide)hide();}
        if(folder.getChildFile("show.request").existsAsFile()){folder.getChildFile("show.request").deleteFile();if(show)show();}
        if(folder.getChildFile("quit.request").existsAsFile()){folder.getChildFile("quit.request").deleteFile();JUCEApplication::quit();return;}
        const auto saveRequest=folder.getChildFile("save.request");
        if(saveRequest.existsAsFile()){
            const String token=saveRequest.loadFileAsString();saveRequest.deleteFile();const bool ok=save();
            auto* ack=new DynamicObject;ack->setProperty("token",token);ack->setProperty("pid",int64(getpid()));ack->setProperty("ok",ok);
            TemporaryFile reply(folder.getChildFile("save.reply.json"));if(reply.getFile().replaceWithText(JSON::toString(var(ack))))reply.overwriteTargetFileWithTemporary();
        }
        const double next=requestedRate.load();if(next!=rate){std::lock_guard<std::mutex> guard(mutex);rate=next;for(auto& s:slots){s.plugin->releaseResources();s.plugin->setRateAndBufferSizeDetails(rate,maxFrames);s.plugin->prepareToPlay(rate,maxFrames);}}
        if(++ticks%4==0){
            const auto count=callbacks.load(std::memory_order_relaxed);
            stalledTicks=streaming&&count==lastCallbacks?stalledTicks+1:0;lastCallbacks=count;
            // If the audio thread hangs, stop claiming the rack is healthy even
            // when its GUI remains responsive. The controller restores dry audio.
            if(stalledTicks<5){
                pulse();
            }
        }
        // Some Windows editors synchronously service state requests on their
        // UI thread. Defer automatic snapshots until the original UI is hidden.
        const bool editorOpen=std::any_of(editors.begin(),editors.end(),[](const auto& e){return e->deferAutosave&&e->isVisible();});
        if(!editorOpen&&stateDirty.load()&&(editorWasOpen||ticks%40==0))save();
        editorWasOpen=editorOpen;
        if(ticks%40==0)publishLatency();
    }
};
class Window final : public DocumentWindow {
    Rack& owner;
public:Window(Rack& rack):DocumentWindow(rack.title+" - VST FX",Colour(0xff344a57),DocumentWindow::closeButton|DocumentWindow::minimiseButton),owner(rack){setUsingNativeTitleBar(true);setContentNonOwned(&rack,true);centreWithSize(850,190);setResizable(true,false);visibilityChanged();}
void closeButtonPressed() override{setVisible(false);}
void visibilityChanged() override{owner.publishVisibility(isVisible());}
};
class App final : public JUCEApplication {
    std::unique_ptr<Rack> rack;std::unique_ptr<Window> window;
public:const String getApplicationName() override{return "Voicemixer Plugin Host";}const String getApplicationVersion() override{return "0.1";}
void initialise(const String& args) override{
    StringArray a;a.addTokens(args,true);a.trim();a.removeEmptyStrings();
    if(a.size()==2&&a[0]=="--probe"){
        AudioPluginFormatManager formats;formats.addDefaultFormats();OwnedArray<PluginDescription> found;
        for(int i=0;i<formats.getNumFormats();++i)formats.getFormat(i)->findAllTypesForFile(found,a[1].unquoted());
        Array<var> result;for(const auto* d:found)result.add(d->createXml()->toString());
        std::cout<<JSON::toString(var(result))<<std::endl;quit();return;
    }
    if(a.size()<3){std::cerr<<"Usage: linux-mixer-plugin-host <rack-folder> <node-name> <label> [--background]\n";setApplicationReturnValue(2);quit();return;}
    rack=std::make_unique<Rack>(File(a[0].unquoted()),a[1].unquoted(),a[2].unquoted());
    rack->show=[this]{
        if(!window)window=std::make_unique<Window>(*rack);
        window->setVisible(true);window->toFront(true);
    };
    rack->hide=[this]{if(window)window->setVisible(false);};
    // Background audio racks have no top-level native peer. Creating a peer
    // and immediately hiding it can leave a dead startup surface on XWayland.
    if(!a.contains("--background"))rack->show();
}
void shutdown() override{window.reset();rack.reset();}
};
}
START_JUCE_APPLICATION(App)
