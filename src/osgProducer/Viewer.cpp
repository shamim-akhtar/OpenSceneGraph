#include <osgProducer/Viewer>
#include <osgProducer/FrameStatsHandler>
#include <osgProducer/StatsEventHandler>

#include <osg/LightSource>

#include <osgUtil/UpdateVisitor>

#include <osgDB/Registry>

#include <osgGA/AnimationPathManipulator>
#include <osgGA/TrackballManipulator>
#include <osgGA/FlightManipulator>
#include <osgGA/DriveManipulator>
#include <osgGA/StateSetManipulator>

using namespace osgProducer;


Viewer::Viewer():
    _done(0),
    _frameNumber(0),
    _kbmcb(0)
{
}

Viewer::Viewer(Producer::CameraConfig *cfg):
    CameraGroup(cfg),
    _done(false),
    _frameNumber(0),
    _kbmcb(0)
{
}

Viewer::Viewer(const std::string& configFile):
    CameraGroup(configFile),
    _done(false),
    _frameNumber(0),
    _kbmcb(0)
{
}

Viewer::Viewer(osg::ArgumentParser& arguments):
    CameraGroup(arguments),
    _done(false),
    _frameNumber(0),
    _kbmcb(0)
{
    osg::DisplaySettings::instance()->readCommandLine(arguments);

    std::string pathfile;
    while (arguments.read("-p",pathfile))
    {
	osg::ref_ptr<osgGA::AnimationPathManipulator> apm = new osgGA::AnimationPathManipulator(pathfile);
	if( apm.valid() && apm->valid() ) 
        {
            unsigned int num = addCameraManipulator(apm.get());
            selectCameraManipulator(num);
        }
    }
}


void Viewer::setUpViewer(unsigned int options)
{

    // set up the keyboard and mouse handling.
    Producer::InputArea *ia = getCameraConfig()->getInputArea();
    Producer::KeyboardMouse *kbm = ia ?
                                   (new Producer::KeyboardMouse(ia)) : 
                                   (new Producer::KeyboardMouse(getCamera(0)->getRenderSurface()));

    // set up the time and frame counter.
    _frameNumber = 0;
    _start_tick = _timer.tick();

    // set the keyboard mouse callback to catch the events from the windows.
    _kbmcb = new osgProducer::KeyboardMouseCallback(_done);
    _kbmcb->setStartTick(_start_tick);
    
    // register the callback with the keyboard mouse manger.
    kbm->setCallback( _kbmcb );
    //kbm->allowContinuousMouseMotionUpdate(true);
    kbm->startThread();



    // set the globa state
    
    osg::ref_ptr<osg::StateSet> globalStateSet = new osg::StateSet;
    setGlobalStateSet(globalStateSet.get());
    {
        globalStateSet->setGlobalDefaults();
        // enable depth testing by default.
        globalStateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);

        // set up an alphafunc by default to speed up blending operations.
        osg::AlphaFunc* alphafunc = new osg::AlphaFunc;
        alphafunc->setFunction(osg::AlphaFunc::GREATER,0.0f);
        globalStateSet->setAttributeAndModes(alphafunc, osg::StateAttribute::ON);
    }
    
    
    // add either a headlight or sun light to the scene.
    osg::LightSource* lightsource = new osg::LightSource;
    setSceneDecorator(lightsource);
    {
        osg::Light* light = new osg::Light;
        lightsource->setLight(light);
        lightsource->setReferenceFrame(osg::LightSource::RELATIVE_TO_ABSOLUTE); // headlight.
        lightsource->setLocalStateSetModes(osg::StateAttribute::ON);
    }

    
    
    _updateVisitor = new osgUtil::UpdateVisitor;
    _updateVisitor->setFrameStamp(_frameStamp.get());

    // create a camera to use with the manipulators.
    _old_style_osg_camera = new osg::Camera;

    if (options&TRACKBALL_MANIPULATOR) addCameraManipulator(new osgGA::TrackballManipulator);
    if (options&FLIGHT_MANIPULATOR) addCameraManipulator(new osgGA::FlightManipulator);
    if (options&DRIVE_MANIPULATOR) addCameraManipulator(new osgGA::DriveManipulator);
    
    if (options&STATE_MANIPULATOR)
    {
        osg::ref_ptr<osgGA::StateSetManipulator> statesetManipulator = new osgGA::StateSetManipulator;
        statesetManipulator->setStateSet(getGlobalStateSet());
        _eventHandlerList.push_back(statesetManipulator.get());
    }
    
    if (options&STATS_MANIPULATOR)
    {
        // register the drawing of stats to pipe 0.
        FrameStatsHandler* fsh = new FrameStatsHandler;
        setStatsHandler(fsh);
        getCamera(0)->addPostDrawCallback(fsh);

        // register the event handler for stats.
        getEventHandlerList().push_back(new StatsEventHandler(this));
        
        
    }
}

unsigned int Viewer::addCameraManipulator(osgGA::CameraManipulator* cm)
{
    if (!cm) return 0xfffff;
    
    // create a key switch manipulator if one doesn't already exist.
    if (!_keyswitchManipulator)
    {
        _keyswitchManipulator = new osgGA::KeySwitchCameraManipulator;
        _eventHandlerList.push_back(_keyswitchManipulator.get());
    }
    
    unsigned int num = _keyswitchManipulator->getNumCameraManipualtors();
    _keyswitchManipulator->addNumberedCameraManipulator(cm);
    
    return num;
}


void Viewer::realize( ThreadingModel thread_model)
{
    if (_keyswitchManipulator.valid() && _keyswitchManipulator->getCurrentCameraManipulator())
    {
        osg::ref_ptr<osgProducer::EventAdapter> init_event = new osgProducer::EventAdapter;
        init_event->adaptFrame(0.0);

        _keyswitchManipulator->setCamera(_old_style_osg_camera.get());
        _keyswitchManipulator->setNode(getSceneDecorator());
        _keyswitchManipulator->home(*init_event,*this);
    }

    CameraGroup::realize( thread_model );
    
    
}

void Viewer::sync()
{
    CameraGroup::sync();

    // set the frame stamp for the new frame.
    double time_since_start = _timer.delta_s(_start_tick,_timer.tick());
    _frameStamp->setFrameNumber(_frameNumber++);
    _frameStamp->setReferenceTime(time_since_start);
}        

void Viewer::update()
{
    // get the event since the last frame.
    osgProducer::KeyboardMouseCallback::EventQueue queue;
    if (_kbmcb) _kbmcb->getEventQueue(queue);

    // create an event to signal the new frame.
    osg::ref_ptr<osgProducer::EventAdapter> frame_event = new osgProducer::EventAdapter;
    frame_event->adaptFrame(_frameStamp->getReferenceTime());
    queue.push_back(frame_event);

    // dispatch the events in order of arrival.
    for(osgProducer::KeyboardMouseCallback::EventQueue::iterator event_itr=queue.begin();
        event_itr!=queue.end();
        ++event_itr)
    {
        bool handled = false;
        for(EventHandlerList::iterator handler_itr=_eventHandlerList.begin();
            handler_itr!=_eventHandlerList.end() && !handled;
            ++handler_itr)
        {   
            handled = (*handler_itr)->handle(*(*event_itr),*this);
        }
    }

    // update the scene by traversing it with the the update visitor which will
    // call all node update callbacks and animations.
    getSceneData()->accept(*_updateVisitor);

    // update the main producer camera
    if (_old_style_osg_camera.valid()) setView(_old_style_osg_camera->getModelViewMatrix().ptr());
}

void Viewer::selectCameraManipulator(unsigned int no)
{
    if (_keyswitchManipulator.valid()) _keyswitchManipulator->selectCameraManipulator(no);
}

void Viewer::requestWarpPointer(int x,int y)
{
    osg::notify(osg::WARN) << "Warning: requestWarpPointer("<<x<<","<<y<<") not implemented yet."<<std::endl;
}


