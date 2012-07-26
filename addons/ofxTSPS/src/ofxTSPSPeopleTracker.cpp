
#include "ofxTSPSPeopleTracker.h"
#include "CPUImageFilter.h"

//scales down tracking images for improved performance
#define TRACKING_SCALE_FACTOR .5

//Fix for FMAX not in Visual Studio C++
#if defined _MSC_VER
#define fmax max
#define fmin min
#pragma warning (disable:4996)
#define snprintf sprintf_s
#endif

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
#pragma mark Setup

//---------------------------------------------------------------------------
ofxTSPSPeopleTracker::ofxTSPSPeopleTracker(){
    p_Settings = NULL;
    hasMouseEvents = false;
}

//---------------------------------------------------------------------------
ofxTSPSPeopleTracker::~ofxTSPSPeopleTracker(){
    if ( hasMouseEvents ){
        hasMouseEvents = false;
        ofRemoveListener(ofEvents().mousePressed, this, &ofxTSPSPeopleTracker::mousePressed);
    }
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setup(int w, int h, string settingsfile)
{	
	ofAddListener(ofEvents().mousePressed, this, &ofxTSPSPeopleTracker::mousePressed);
	hasMouseEvents = true;
    
	width  = w;
	height = h;
	
	grayImage.allocate(width, height);
	colorImage.allocate(width,height);
	grayImageWarped.allocate(width, height);
	colorImageWarped.allocate(width,height);
	grayBg.allocate(width, height);
	grayDiff.allocate(width, height);
	canny.allocate(width, height);
	floatBgImg.allocate(width, height);
	graySmallImage.allocate( width*TRACKING_SCALE_FACTOR, height*TRACKING_SCALE_FACTOR );	
	grayLastImage.allocate( width*TRACKING_SCALE_FACTOR, height*TRACKING_SCALE_FACTOR );
	grayBabyImage.allocate( width*TRACKING_SCALE_FACTOR, height*TRACKING_SCALE_FACTOR );
	
	//set up optical flow
	opticalFlow.allocate( width*TRACKING_SCALE_FACTOR, height*TRACKING_SCALE_FACTOR );
	opticalFlow.setCalcStep(5,5);
	grayLastImage = graySmallImage;
	
	//set tracker
  bOscEnabled = bTuioEnabled = bTcpEnabled = bWebSocketServerEnabled = bWebSocketClientEnabled = false;
	p_Settings = gui.getSettings();

  //set up canny
  lines = 0;
  storage = cvCreateMemStorage(0);
	
	//gui.loadFromXML();	
	//gui.setDraw(true);		
	
	//setup gui quad in manager
	gui.setup();
	gui.setupQuadGui( width, height );
	gui.loadSettings( settingsfile );
	activeHeight = ofGetHeight();
	activeWidth = ofGetWidth();
	
	activeViewIndex = 4;
	
	//setup view rectangles 
	
	cameraView.setup(width, height);
	adjustedView.setup(width, height);
	bgView.setup(width, height);
	processedView.setup(width, height);
	dataView.setup(width, height);
	
	updateViewRectangles();
	
	cameraView.setImage(colorImage);
	cameraView.setTitle("Camera Source View", "Camera");
	cameraView.setColor(218,173,90);
	
	adjustedView.setImage(grayImageWarped);
	adjustedView.setTitle("Adjusted Camera View", "Adjusted");
	adjustedView.setColor(174,139,138);
	
	bgView.setImage(grayBg);
	bgView.setTitle("Background Reference View", "Background");
	bgView.setColor(213,105,68);
		
	processedView.setImage(grayDiff);
	processedView.setTitle("Differenced View", "Differencing");
	processedView.setColor(113,171,154);
	
	dataView.setImage(canny);
	dataView.setTitle("Data View", "Data");
	dataView.setColor(191,120,0);
	
	setActiveView(PROCESSED_VIEW);
	
    persistentTracker.setListener( this );
	//updateSettings();
	lastHaarFile = "";
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::resize( int w, int h ){
    width  = w;
	height = h;
	
	grayImage.allocate(width, height);
	colorImage.allocate(width,height);
	grayImageWarped.allocate(width, height);
	colorImageWarped.allocate(width,height);
	grayBg.allocate(width, height);
	grayDiff.allocate(width, height);
	floatBgImg.allocate(width, height);
	graySmallImage.allocate( width*TRACKING_SCALE_FACTOR, height*TRACKING_SCALE_FACTOR );	
	grayLastImage.allocate( width*TRACKING_SCALE_FACTOR, height*TRACKING_SCALE_FACTOR );
	grayBabyImage.allocate( width*TRACKING_SCALE_FACTOR, height*TRACKING_SCALE_FACTOR );
	
	//set up optical flow
	opticalFlow.allocate( width*TRACKING_SCALE_FACTOR, height*TRACKING_SCALE_FACTOR );
	opticalFlow.setCalcStep(5,5);
	grayLastImage = graySmallImage;
    
	gui.setupQuadGui( width, height );
	
	activeViewIndex = 4;
	
	//setup view rectangles 
	
	cameraView.setup(width, height);
	adjustedView.setup(width, height);
	bgView.setup(width, height);
	processedView.setup(width, height);
	dataView.setup(width, height);
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setHaarXMLFile(string haarFile)
{
	haarFile = "haar/" + haarFile;
	
	//check if haar file has changed
	if(lastHaarFile != haarFile){
		ofLog(OF_LOG_VERBOSE, "changing haar file to " + haarFile);
		haarFinder.setup(haarFile);
		haarTracker.setup(&haarFinder);
		lastHaarFile = haarFile;
	}
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
#pragma mark Setup Communication

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setupTuio(string ip, int port)
{
	ofLog(OF_LOG_VERBOSE, "SEND TUIO");
	bTuioEnabled = true;
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->oscPort = port;
	p_Settings->oscHost = ip;
	tuioClient.setup(ip, port);
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setupOsc(string ip, int port)
{
	ofLog(OF_LOG_VERBOSE, "SEND OSC");
	bOscEnabled = true;
    if (p_Settings == NULL) p_Settings = gui.getSettings();
    p_Settings->oscPort = port;
	p_Settings->oscHost = ip;
	oscClient.setupSender(ip, port);
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setupTcp(int port)
{
	bTcpEnabled = true;
	ofLog(OF_LOG_VERBOSE, "SEND TCP TO PORT "+port);
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->tcpPort = port;
	tcpClient.setup(port);
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setupWebSocketServer( int port)
{
	ofLog(OF_LOG_VERBOSE, "SEND WEBSOCKET SERVER ON PORT "+port);
    if (p_Settings == NULL) p_Settings = gui.getSettings();
    bWebSocketServerEnabled = true;
	p_Settings->webSocketServerPort = port;
    gui.setValueI("WSS_PORT", port);
    bWebSocketServerEnabled = webSocketServer.setupServer(port);
    p_Settings->bSendWebSocketServer = bWebSocketServerEnabled;
    gui.setValueB("SEND_WSS", p_Settings->bSendWebSocketServer);
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setupWebSocketClient( string host, int port, bool bUseSSL, string channel) {
	ofLog(OF_LOG_VERBOSE, "SEND WEBSOCKET CLIENT AT ws://"+host+channel+":"+ofToString( port ));
    if (p_Settings == NULL) p_Settings = gui.getSettings();
    bWebSocketClientEnabled = true;
    p_Settings->webSocketHost = host;
	p_Settings->webSocketPort = port;
	p_Settings->webSocketChannel = channel;
    p_Settings->webSocketUseSSL = bUseSSL;
    bWebSocketClientEnabled = webSocketServer.setupClient( host, port, false, channel);
    p_Settings->bSendWebSocketClient = bWebSocketClientEnabled;
    gui.setValueS("WS_HOST", host);
    gui.setValueS("WS_PORT", ofToString( port ));
    gui.setValueS("WS_CHANNEL", channel);
    gui.setValueB("WS_USESSL", bUseSSL);
    gui.setValueB("SEND_WS", p_Settings->bSendWebSocketClient);
}


//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setListener(ofxPersonListener* listener)
{
	eventListener = listener;
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
#pragma mark Track People
//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::update(ofxCvColorImage image)
{
	grayImage = image;
	colorImage = image;
	updateSettings();
	trackPeople();
	
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::update(ofxCvGrayscaleImage image)
{
	grayImage = image;
	
	updateSettings();
	trackPeople();
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::updateSettings()
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	setHaarXMLFile(p_Settings->haarFile);

	//check to enable OSC
	if (p_Settings->bSendOsc && !bOscEnabled) setupOsc(p_Settings->oscHost, p_Settings->oscPort);
	else if (!p_Settings->bSendOsc) bOscEnabled = false;
	
	//check to enable TUIO
	if (p_Settings->bSendTuio && !bTuioEnabled) setupTuio(p_Settings->tuioHost, p_Settings->tuioPort);
	else if (!p_Settings->bSendTuio) bTuioEnabled = false;

	//check to enable TCP
	if (p_Settings->bSendTcp && !bTcpEnabled) setupTcp(p_Settings->tcpPort);
	else if (!p_Settings->bSendTcp) bTcpEnabled = false;
        
    //check to enable websockets
    if (p_Settings->bSendWebSocketClient && !bWebSocketClientEnabled){
        setupWebSocketClient(p_Settings->webSocketHost, p_Settings->webSocketPort, p_Settings->webSocketUseSSL, p_Settings->webSocketChannel);
    } else if (!p_Settings->bSendWebSocketClient){
        bWebSocketClientEnabled = false;
        webSocketServer.closeClient();
    }
    if (p_Settings->bSendWebSocketServer && !bWebSocketServerEnabled){
        setupWebSocketServer(p_Settings->webSocketServerPort);
    } 
    if (!p_Settings->bSendWebSocketServer){
        bWebSocketServerEnabled = false;
        webSocketServer.closeServer();
    }
    
	//switch camera view if new panel is selected
	if (p_Settings->currentPanel != p_Settings->lastCurrentPanel) setActiveView(p_Settings->currentPanel + 1);

	// Set the current view within the gui so the image can only be warped when in Camera View
	if (cameraView.isActive()) {
		gui.changeGuiCameraView(true);
	} else {
		gui.changeGuiCameraView(false);
	}
}

/**
 * Core Method
 * Run every frame to update
 * the system to the current location
 * of people
 */
//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::trackPeople()
{	
    if (p_Settings == NULL) p_Settings = gui.getSettings();
    
	//-------------------
	//QUAD WARPING
	//-------------------
		
	//warp background
    //grayImageWarped = grayImage;
    //colorImage = grayImage;
    colorImageWarped = colorImage;
    //getQuadSubImage(&colorImage, &colorImageWarped, &p_Settings->quadWarpScaled, 3);
    getQuadSubImage(&grayImage, &grayImageWarped, &p_Settings->quadWarpScaled, 1);
    //grayImageWarped.warpIntoMe(grayImage, p_Settings->quadWarpScaled,     p_Settings->quadWarpOriginal);
	//colorImageWarped.warpIntoMe(colorImage, p_Settings->quadWarpScaled, p_Settings->quadWarpOriginal);	
	
	graySmallImage.scaleIntoMe(grayImageWarped);
	grayBabyImage.scaleIntoMe(grayImageWarped);
	
	grayDiff = grayImageWarped;
	
	//amplify (see cpuimagefilter class)
	if(p_Settings->bAmplify){
		grayDiff.amplify(grayDiff, p_Settings->highpassAmp/15.0f);
	}
	
	grayImageWarped = grayDiff;
	
	//-------------------
	//BACKGROUND
	//-------------------
	
	//force learn background if there are > 5 blobs (off by default)
	//JG Disabling this feature for now, 
	//I think it's a great idea but it needs to be better described and "5" needs to be customizable
//	if (p_Settings->bSmartLearnBackground == true && contourFinder.nBlobs > 5){
//		p_Settings->bLearnBackground = true;
	//	}
	
	//learn background (either in reset or additive)
	if (p_Settings->bLearnBackground){
		cout << "Learning Background" << endl;
		grayBg = grayImageWarped;
	}
    
    if (p_Settings->bBlankBackground){
        grayBg -= grayBg;
    }
	
	//progressive relearn background
	if (p_Settings->bLearnBackgroundProgressive){
		if (p_Settings->bLearnBackground) floatBgImg = grayBg;
		floatBgImg.addWeighted( grayImageWarped, p_Settings->fLearnRate * .00001);
		grayBg = floatBgImg;
		//cvConvertScale( floatBgImg.getCvImage(), grayBg.getCvImage(), 255.0f/65535.0f, 0 );       
		//grayBg.flagImageChanged();			
	}
	
	//printf("track type %d from (%d,%d,%d)\n", p_Settings->trackType, TRACK_ABSOLUTE, TRACK_DARK, TRACK_LIGHT);
	if(p_Settings->trackType == TRACK_ABSOLUTE){
		grayDiff.absDiff(grayBg, grayImageWarped);
	}
	else{
		grayDiff = grayImageWarped;
		if(p_Settings->trackType == TRACK_LIGHT){
			//grayDiff = grayBg - grayImageWarped;
			cvSub(grayBg.getCvImage(), grayDiff.getCvImage(), grayDiff.getCvImage());
		}
		else if(p_Settings->trackType == TRACK_DARK){ 
			cvSub(grayDiff.getCvImage(), grayBg.getCvImage(), grayDiff.getCvImage());
			//grayDiff = grayImageWarped - grayBg;
		}
		grayDiff.flagImageChanged();
	}
	
	//-----------------------
	// IMAGE TREATMENT
	//-----------------------
	if(p_Settings->bSmooth){
		grayDiff.blur((p_Settings->smooth * 2) + 1); //needs to be an odd number
	}
	
	//highpass filter (see cpuimagefilter class)	
	if(p_Settings->bHighpass){
		grayDiff.highpass(p_Settings->highpassBlur, p_Settings->highpassNoise);
	}
	
	//threshold	
	if (p_Settings->trackType != NO_DIFFERENCING){
    grayDiff.threshold(p_Settings->threshold);
  }

  //Edge detector
	if(p_Settings->bEdgeDetection){
    cvCanny(grayDiff.getCvImage(), grayDiff.getCvImage(), p_Settings->edgeThres1, p_Settings->edgeThres2);
    cvDilate(grayDiff.getCvImage(), grayDiff.getCvImage() , 0, p_Settings->dilatation);
  }

  //-----------------------
	// TRACKING
	//-----------------------	

	//find the optical flow
	if ( p_Settings->bTrackOpticalFlow ){
		opticalFlow.calc(grayLastImage, graySmallImage, 11);
	}
	
	//accumulate and store all found haar features.
	vector<ofRectangle> haarRects;
	if(p_Settings->bDetectHaar){
		haarTracker.findHaarObjects( grayBabyImage );
		float x, y, w, h;
		while(haarTracker.hasNextHaarItem()){
			haarTracker.getHaarItemPropertiesEased( &x, &y, &w, &h );
			haarRects.push_back( ofRectangle(x,y,w,h) );
		}
	}
	
	char pringString[1024];
	sprintf(pringString, "found %i haar items this frame", (int) haarRects.size());
	ofLog(OF_LOG_VERBOSE, pringString);
	
	contourFinder.findContours(grayDiff, p_Settings->minBlob*width*height, p_Settings->maxBlob*width*height, 50, p_Settings->bFindHoles);
	persistentTracker.trackBlobs(contourFinder.blobs);
    
	// By setting maxVector and minVector outside the following for-loop, blobs do NOT have to be detected first
	//            before optical flow can begin working.
	if( p_Settings->bTrackOpticalFlow ) {
        scene.averageMotion = opticalFlow.flowInRegion(0,0,width,height);
        scene.percentCovered = 0; 
        opticalFlow.maxVector = p_Settings->maxOpticalFlow;
		opticalFlow.minVector = p_Settings->minOpticalFlow;
	}
    	
	for(int i = 0; i < persistentTracker.blobs.size(); i++){
		ofxCvTrackedBlob blob = persistentTracker.blobs[i];
		ofxTSPSPerson* p = getTrackedPerson(blob.id);
		//somehow we are not tracking this person, safeguard (shouldn't happen)
		if(NULL == p){
			ofLog(OF_LOG_WARNING, "ofxPerson::warning. encountered persistent blob without a person behind them\n");
			continue;
		}
		
		scene.percentCovered += blob.area;
		
		//update this person with new blob info
		p->update(blob, p_Settings->bCentroidDampen);

		//normalize simple contour
		for (int i=0; i<p->simpleContour.size(); i++){
			p->simpleContour[i].x /= width;
			p->simpleContour[i].y /= height;
		}
		
        //find peak in blob (only useful with Kinect)
        CvPoint minLoc, maxLoc;
        double minVal = 0, maxVal = 0;
        grayImageWarped.setROI( p->boundingRect );
        cvMinMaxLoc( grayImageWarped.getCvImage(), &minVal, &maxVal, &minLoc, &maxLoc, 0);
        
        // set highest and lowest points: x, y, VALUE stored in .z prop
        // ease vals unless first time you're setting them
        if ( p->highest.x == -1 ){
            p->highest.set(  p->boundingRect.x + maxLoc.x,  p->boundingRect.y + maxLoc.y, maxVal);
            p->lowest.set(  p->boundingRect.x + minLoc.x,  p->boundingRect.y + minLoc.y, minVal);
        } else {
            p->highest.x = ( p->highest.x * .7 ) + ( p->boundingRect.x + maxLoc.x ) * .3;
            p->highest.y = ( p->highest.y * .7 ) + ( p->boundingRect.y + maxLoc.y ) * .3;
            p->highest.z = ( p->highest.z * .7) + ( maxVal ) * .3;
            p->lowest.x = ( p->lowest.x * .7 ) + ( p->boundingRect.x + minLoc.x ) * .3;
            p->lowest.y = ( p->lowest.y * .7 ) + ( p->boundingRect.y + minLoc.y ) * .3;
            p->lowest.z = ( p->lowest.z * .7) + ( minVal ) * .3;            
        }
        
        // set ROI for opticalflow and haar
        ofRectangle roi;
		roi.x		= fmax( (p->boundingRect.x - p_Settings->haarAreaPadding) * TRACKING_SCALE_FACTOR, 0.0f );
		roi.y		= fmax( (p->boundingRect.y - p_Settings->haarAreaPadding) * TRACKING_SCALE_FACTOR, 0.0f );
		roi.width	= fmin( (p->boundingRect.width  + p_Settings->haarAreaPadding*2) * TRACKING_SCALE_FACTOR, grayBabyImage.width - roi.x );
		roi.height	= fmin( (p->boundingRect.height + p_Settings->haarAreaPadding*2) * TRACKING_SCALE_FACTOR, grayBabyImage.width - roi.y );	
		
		//sum optical flow for the person
		if(p_Settings->bTrackOpticalFlow){
			p->opticalFlowVectorAccumulation = opticalFlow.flowInRegion(roi);
		}
        
		//detect haar patterns (faces, eyes, etc)
		if (p_Settings->bDetectHaar){
			bool bHaarItemSet = false;
				
			//find the region of interest, expanded by haarArea.
			bool haarThisFrame = false;
			for(int i = 0; i < haarRects.size(); i++){
				ofRectangle hr = haarRects[i];
				//check to see if the haar is contained within the bounding rectangle
				if(hr.x > roi.x && hr.y > roi.y && hr.x+hr.width < roi.x+roi.width && hr.y+hr.height < roi.y+roi.height){
					hr.x /= TRACKING_SCALE_FACTOR;
					hr.y /= TRACKING_SCALE_FACTOR;
					hr.width /= TRACKING_SCALE_FACTOR;
					hr.height /= TRACKING_SCALE_FACTOR;
					p->setHaarRect(hr);
					haarThisFrame = true;
					break;
				}
			}
			if(!haarThisFrame){
				p->noHaarThisFrame();
			}
			/*
			 //JG 1/28/2010
			 //This is the prper way to do the Haar, checking one person at a time.
			 //however this discards the robustness of the haarFinder and 
			 //makes the whole operation really spotty.  
			 // The solution is to put more energy into finding out how 
			 // the haar tracker works to get robust/persistent haar items over time.
			 //for now we just check the whole screen and see if the haar is contained
			grayBabyImage.setROI(roi.x, roi.y, roi.width, roi.height);
			int numFound = haarFinder.findHaarObjects(grayBabyImage, roi);
			//cout << "found " << numFound << " for this object" << endl;
			if(numFound > 0) {
				ofRectangle haarRect = haarFinder.blobs[0].boundingRect;
				haarRect.x /= TRACKING_SCALE_FACTOR;
				haarRect.y /= TRACKING_SCALE_FACTOR;
				haarRect.width /= TRACKING_SCALE_FACTOR;
				haarRect.height /= TRACKING_SCALE_FACTOR;
				p->setHaarRect(haarRect);
			}
			else {
				p->noHaarThisFrame();
			}
			 */
		}
		
		if(eventListener != NULL){
			if( p->velocity.x != 0 || p->velocity.y != 0){
				eventListener->personMoved(p, &scene);
			}
			eventListener->personUpdated(p, &scene);
		}
	}
	
    // reset roi of image
    grayImageWarped.setROI(0,0,width,height);
    
	//normalize it
	scene.percentCovered /= width*height;
	
  //TRACK FISHES
	if (p_Settings->bTrackArms){
    //DETECT LINES WITH HOUGH TRANSFORM
    lines = cvHoughLines2(grayDiff.getCvImage(), storage, CV_HOUGH_STANDARD, 1, CV_PI/180, 1 + p_Settings->minArm*width*height/100.);
    float totalAngles = 0;
    float totalCos = 0;
    float totalSin = 0;
    totalTheta = 0;
    int nbofValidLines = 0;
    for( int i = 0; i < MIN(lines->total,10000); i++ ) {
      float* line = (float*)cvGetSeqElem(lines,i);
      float theta = line[1];
      totalSin += sin(theta);
      totalCos += cos(theta);
      theta = (theta > CV_PI/2) ? theta -CV_PI : theta;
      if (theta > p_Settings->leftHorizThres && theta < p_Settings->rightHorizThres){
        totalTheta += theta;
        nbofValidLines++;
      }
    }
    totalSin/= nbofValidLines;//sMIN(lines->total,10000);
    totalCos/= nbofValidLines;//MIN(lines->total,10000);
    totalTheta/= nbofValidLines;//MIN(lines->total,10000);
    //cout << "Total Theta: " << totalTheta << endl;
    //totalTheta = atan2(totalSin,totalCos);
    //cout << "Total ThetaTan: " << totalTheta << endl;
  }
	//-----------------------
	// VIEWS
	//-----------------------	
    
	//store the old image
	grayLastImage = graySmallImage;
	
	//update views
	
	cameraView.update(colorImage);
	if (p_Settings->bAdjustedViewInColor)
		adjustedView.update(colorImageWarped);
	else
		adjustedView.update(grayImageWarped);
	bgView.update(grayBg);
	processedView.update(grayDiff);
  dataView.update(grayDiff);
	//-----------------------
	// COMMUNICATION
	//-----------------------	
  //persons
    for (int i = 0; i < trackedPeople.size(); i++){
        ofxTSPSPerson* p = trackedPeople[i];
        ofPoint centroid = p->getCentroidNormalized(width, height);
//			if(p_Settings->bUseHaarAsCenter && p->hasHaarRect()){
        
        if (bTuioEnabled){
            ofPoint tuioCursor = p->getCentroidNormalized(width, height);
            tuioClient.cursorDragged( tuioCursor.x, tuioCursor.y, p->oid);
        }
        
        if (bOscEnabled){
            if( p->velocity.x != 0 || p->velocity.y != 0){
				//DEPRECATED:
                oscClient.personMoved(p, centroid, width, height, p_Settings->bSendOscContours);
            }
            oscClient.personUpdated(p, centroid, width, height, p_Settings->bSendOscContours);
        }
        
        if (bTcpEnabled){
            tcpClient.personUpdated(p, centroid, width, height, p_Settings->bSendOscContours);
        }
        
        if ( bWebSocketClientEnabled || bWebSocketServerEnabled ){
            webSocketServer.personUpdated(p, centroid, width, height, p_Settings->bSendOscContours);
        }
    }
  //fishes
  if (p_Settings->bSenseLeftRight && bOscEnabled){
      //left
      if (totalTheta < p_Settings->leftAngleThres){
        oscClient.goLeft();
      //right
      } else if (totalTheta < CV_PI/2 && totalTheta > p_Settings->rightAngleThres){
        oscClient.goRight();
      //straight
      } else {
        oscClient.goStraight();
      }
  }
    
  //setup
	if(bTuioEnabled){
		tuioClient.update();		
	}
	
	if (bOscEnabled){
		oscClient.ip = p_Settings->oscHost;
		oscClient.port = p_Settings->oscPort;
		oscClient.useLegacy = p_Settings->bUseLegacyOsc;
		oscClient.update();
	};
    
	if (bTcpEnabled){
		tcpClient.port = p_Settings->oscPort;
		tcpClient.update();
		tcpClient.send();
	}
    
    if ( bWebSocketClientEnabled || bWebSocketServerEnabled ){
        if ( bWebSocketClientEnabled && (p_Settings->webSocketPort != webSocketServer.getPort() || p_Settings->webSocketHost != webSocketServer.getHost()) ){
            webSocketServer.closeClient();
            setupWebSocketClient( p_Settings->webSocketHost, p_Settings->webSocketPort );
        }
        
        if ( bWebSocketServerEnabled && p_Settings->webSocketServerPort != webSocketServer.getServerPort() ){
            webSocketServer.closeServer();
            setupWebSocketServer( p_Settings->webSocketServerPort );
        }
        
        //sent automagically
        webSocketServer.send();
    }
}

void ofxTSPSPeopleTracker::goLeft(){
	if (bOscEnabled){
      oscClient.goLeft();
  }
};
void ofxTSPSPeopleTracker::goRight(){
	if (bOscEnabled){
      oscClient.goRight();
  }
};
void ofxTSPSPeopleTracker::goStraight(){
	if (bOscEnabled){
      oscClient.goStraight();
  }
};

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
#pragma mark Person Management
//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::blobOn( int x, int y, int id, int order )
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	ofxCvTrackedBlob blob = persistentTracker.getById( id );
	ofxTSPSPerson* newPerson = new ofxTSPSPerson(id, order, blob);
	if(eventListener != NULL){
		eventListener->personEntered(newPerson, &scene);
	}
    
    ofPoint centroid = newPerson->getCentroidNormalized(width, height);
    
	if(bTuioEnabled){
		tuioClient.cursorPressed(1.0*x/width, 1.0*y/height, order);
	}
	if(bOscEnabled){
		oscClient.personEntered(newPerson, centroid, width, height, p_Settings->bSendOscContours);
	}
	if(bTcpEnabled){
		tcpClient.personEntered(newPerson, centroid, width, height, p_Settings->bSendOscContours);
	}
	if( bWebSocketClientEnabled || bWebSocketServerEnabled ){
		webSocketServer.personEntered(newPerson, centroid, width, height, p_Settings->bSendOscContours);
	}
	trackedPeople.push_back( newPerson );	
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::blobMoved( int x, int y, int id, int order ){/*not used*/}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::blobOff( int x, int y, int id, int order )
{
	ofxTSPSPerson* p = getTrackedPerson(id);
	//ensure we are tracking
	if(NULL == p){
		ofLog(OF_LOG_WARNING, "ofxPerson::warning. encountered persistent blob " + ofToString( id ) +" without a person behind them\n");		
		return;
	}
    
	//delete the object and remove it from the vector
	std::vector<ofxTSPSPerson*>::iterator it;
	for(it = trackedPeople.begin(); it != trackedPeople.end(); it++){
		if((*it)->pid == p->pid){
			trackedPeople.erase(it);
			break;
		}
	}
	
	//alert the delegate
	if(eventListener != NULL){
		eventListener->personWillLeave(p, &scene);
	}
    
    ofPoint centroid = p->getCentroidNormalized(width, height);
	if (bTuioEnabled) {
		tuioClient.cursorReleased(centroid.x, centroid.y, order);	
	}
	//send osc kill message if enabled
	if (bOscEnabled){
		oscClient.personWillLeave(p, centroid, width, height, p_Settings->bSendOscContours);
	};
	
	//send tcp kill message if enabled
	if(bTcpEnabled){
		tcpClient.personWillLeave(p, centroid, width, height, p_Settings->bSendOscContours);
	}
    
	if( bWebSocketClientEnabled || bWebSocketServerEnabled ){
		webSocketServer.personWillLeave(p, centroid, width, height, p_Settings->bSendOscContours);
	}
    
    // delete pointer
    delete p;
}

//---------------------------------------------------------------------------
ofxTSPSPerson* ofxTSPSPeopleTracker::getTrackedPerson( int pid )
{
    for( int i = 0; i < trackedPeople.size(); i++ ) {
        if( trackedPeople[i]->pid == pid ) {
            return trackedPeople[i];
        }
    }
	return NULL;
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
#pragma mark Draw
//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::draw()
{
	draw(0,0);
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::draw(int x, int y)
{
	draw(x,y,drawMode);
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::draw(int x, int y, int mode)
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
    // run lean + mean if we're minimized
    if (p_Settings->bMinimized) return;
	ofPushMatrix();
		ofTranslate(x, y, 0);
		// draw the incoming, the grayscale, the bg and the thresholded difference
		ofSetHexColor(0xffffff);
	
        // draw gui
        gui.draw();
    
		//draw large image
		if (activeViewIndex ==  CAMERA_SOURCE_VIEW){
			cameraView.drawLarge(activeView.x, activeView.y, activeView.width, activeView.height);		
			//gui.drawQuadGui( activeView.x, activeView.y, activeView.width, activeView.height );
		} else if ( activeViewIndex == ADJUSTED_CAMERA_VIEW){
			adjustedView.drawLarge(activeView.x, activeView.y, activeView.width, activeView.height);				
		} else if ( activeViewIndex == REFERENCE_BACKGROUND_VIEW){
			bgView.drawLarge(activeView.x, activeView.y, activeView.width, activeView.height);			
		} else if ( activeViewIndex == PROCESSED_VIEW){ 
			processedView.drawLarge(activeView.x, activeView.y, activeView.width, activeView.height);
		} else if ( activeViewIndex == DATA_VIEW ){
			dataView.drawLarge(activeView.x, activeView.y, activeView.width, activeView.height);
			ofPushMatrix();
				ofTranslate(activeView.x, activeView.y);
				drawBlobs(activeView.width, activeView.height);
			ofPopMatrix();
		}
		
		//draw all images small
		cameraView.draw();
		adjustedView.draw();
		bgView.draw();
		processedView.draw();
		dataView.draw();	
		
		ofPushMatrix();
			ofTranslate(dataView.x, dataView.y);
			drawBlobs(dataView.width, dataView.height);
		ofPopMatrix();
		
	ofPopMatrix();
	
	//draw framerate in a box
	
	char frmrate[1024];
	sprintf(frmrate, "Frame rate: %f", ofGetFrameRate() );
	
	ofPushStyle();
	ofFill();
	ofSetColor(196,182,142);
	ofRect(cameraView.x, cameraView.y + cameraView.height + spacing*3 + 8, cameraView.width*2 + spacing, spacing*4);
	ofPopStyle();
	
	if (!bFontLoaded) ofDrawBitmapString(frmrate, cameraView.x + 10, cameraView.y + 10 + cameraView.height + spacing*5);
	else font.drawString(frmrate, (int)cameraView.x + 10, (int) (cameraView.y + 10 + cameraView.height + spacing*5));
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::drawBlobs( float drawWidth, float drawHeight){
	
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	float scaleVar = (float) drawWidth/width;
	
	ofFill();
	ofSetHexColor(0x333333);
	ofRect(0,0,drawWidth,drawHeight);
	ofSetHexColor(0xffffff);
	
	ofNoFill();
	
	if (p_Settings->bTrackOpticalFlow){
		ofSetColor(34,151,210);
		opticalFlow.draw(drawWidth,drawHeight);
	}					
	
	ofPushMatrix();
	ofScale(scaleVar, scaleVar);
	
	// simpler way to draw contours: contourFinder.draw();
	
	for (int i=0; i < trackedPeople.size(); i++){
		
		//draw blobs				
		//if haarfinder is looking at these blobs, draw the area it's looking at
		ofxTSPSPerson* p = trackedPeople[i];
		
		//draw contours 
		ofPushStyle();
		ofNoFill();
		if (p_Settings->bSendOscContours){
			ofSetHexColor(0x3abb93);
		} else {
			ofSetHexColor(0xc4b68e);
		}
		ofBeginShape();
		for( int j=0; j<p->contour.size(); j++ ) {
			ofVertex( p->contour[j].x, p->contour[j].y );
		}
		ofEndShape();
		ofPopStyle();
		
		if(p_Settings->bTrackOpticalFlow){
			//purple optical flow arrow
			ofSetHexColor(0xff00ff);
			//JG Doesn't really provide any helpful information since its so scattered
//			ofLine(p->centroid.x, 
//				   p->centroid.y, 
//				   p->centroid.x + p->opticalFlowVectorAccumulation.x, 
//				   p->centroid.y + p->opticalFlowVectorAccumulation.y);
		}
		
        // draw haar
        
		ofSetHexColor(0xffffff);							
		if(p_Settings->bDetectHaar){
			ofSetHexColor(0xee3523);
			//draw haar search area expanded 
			//limit to within data box so it's not confusing				
            ofRectangle haarRect = ofRectangle(p->boundingRect.x - p_Settings->haarAreaPadding, 
                                               p->boundingRect.y - p_Settings->haarAreaPadding, 
                                               p->boundingRect.width  + p_Settings->haarAreaPadding*2, 
                                               p->boundingRect.height + p_Settings->haarAreaPadding*2);
            if (haarRect.x < 0){
                haarRect.width += haarRect.x;
                haarRect.x = 0;					
            }
            if (haarRect.y < 0){
                haarRect.height += haarRect.y;	
                haarRect.y = 0;
            }
            if (haarRect.x + haarRect.width > width) haarRect.width = width-haarRect.x;
            if (haarRect.y + haarRect.height > height) haarRect.height = height-haarRect.y;
            ofRect(haarRect.x, haarRect.y, haarRect.width, haarRect.height);

            if(p->hasHaarRect()){
                //draw the haar rect
                ofSetHexColor(0xee3523);
                ofRect(p->getHaarRect().x, p->getHaarRect().y, p->getHaarRect().width, p->getHaarRect().height);
                //haar-detected people get a red square
                ofSetHexColor(0xfd5f4f);
            } else {
                //no haar gets a yellow square
                ofSetHexColor(0xeeda00);
            }
		} else {
			//no haar gets a yellow square
			ofSetHexColor(0xeeda00);
		}
        		
		//draw person
		ofRect(p->boundingRect.x, p->boundingRect.y, p->boundingRect.width, p->boundingRect.height);
		
        // draw highest point
        ofSetHexColor(0xff00ff);	
        ofCircle(p->highest.x, p->highest.y, 4);
        
		//draw centroid
		ofSetHexColor(0xff0000);
		ofCircle(p->centroid.x, p->centroid.y, 3);
		
		//draw id
		ofSetHexColor(0xffffff);
		char idstr[1024];
		sprintf(idstr, "pid: %d\noid: %d\nage: %d", p->pid, p->oid, p->age );
		ofDrawBitmapString(idstr, p->centroid.x+8, p->centroid.y);													
	}
	ofPopMatrix();
	ofSetHexColor(0xffffff);				
	//ofDrawBitmapString("blobs and optical flow", 5, height - 5 );

  //draw fishes lines
	if (p_Settings->bTrackArms && lines !=0){
    float scaleVar = (float) drawWidth/width;
    ofPushMatrix();
    ofScale(scaleVar, scaleVar);
    ofSetColor(34,151,210);
    for( int i = 0; i < MIN(lines->total,10000); i++ ) {
      float* line = (float*)cvGetSeqElem(lines,i);
      float r = line[0];
      float theta = line[1];
      theta = (theta > CV_PI/2) ? theta -CV_PI : theta;
      if (theta > p_Settings->leftHorizThres && theta < p_Settings->rightHorizThres){
        theta = (theta < 0) ? theta +CV_PI : theta;
        CvPoint pt1, pt2;
        double cos0 = cos(theta), sin0 = sin(theta);
        double rcos0 = r*cos0, rsin0 = r*sin0;
        //pt1.y = 0
        double a = -rsin0/cos0;
        pt1.x = cvRound(rcos0 - a*sin0);
        pt1.y = cvRound(rsin0 + a*cos0);
        //unless it's out of screen
        if (pt1.x < 0){
          a = rcos0/sin0;
        } else if (pt1.x > width){
          a = (rcos0 - width)/sin0;
        }
        pt1.x = cvRound(rcos0 - a*sin0);
        pt1.y = cvRound(rsin0 + a*cos0);
        
        //pt2.y = height
        a = (height-rsin0)/cos0;
        pt2.x = cvRound(rcos0 - a*sin0);
        pt2.y = cvRound(rsin0 + a*cos0);
         //unless it's out of screen
        if (pt2.x < 0){
          a = rcos0/sin0;
        } else if (pt2.x > width){
          a = (rcos0 - width)/sin0;
        }
        pt2.x = cvRound(rcos0 - a*sin0);
        pt2.y = cvRound(rsin0 + a*cos0);
        
        ofLine(pt1.x, pt1.y, pt2.x, pt2.y);
      }
    } 
    //red line for average direction
    if (isnormal(totalTheta)){
      ofSetColor(255, 0, 0);
      drawAngle(totalTheta );
    }
    
    //draw horizontals
    ofSetColor(255, 255, 0);
    drawAngle(p_Settings->leftHorizThres);
    drawAngle(p_Settings->rightHorizThres);
    
    if (p_Settings->bSenseLeftRight){
      //left
      ofSetColor(0, 255, 0);
      drawAngle(p_Settings->leftAngleThres);

      //right
      drawAngle(p_Settings->rightAngleThres);

      //show left or right decision
      int sqsize = 50;
      //left
      if (totalTheta < p_Settings->leftAngleThres){
        ofRect(0, height-sqsize, sqsize, sqsize);
      //right
      } else if (totalTheta < CV_PI/2 && totalTheta > p_Settings->rightAngleThres){
        ofRect(width-sqsize, height-sqsize, sqsize, sqsize);
      //straight
      } else {
        ofRect(width/2-sqsize/2, height-sqsize, sqsize, sqsize);
      }

    }
    ofPopMatrix();
    ofSetHexColor(0xffffff);				
  }
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::drawAngle( float theta ){
  theta += PI/2;
  CvPoint pt1, pt2;
  pt1.x = width/2;
  pt1.y = height;
  float r = width/2;
  double cos0 = cos(theta), sin0 = sin(theta);
  pt2.x = pt1.x + (r * cos0);
  pt2.y = pt1.y + (r * sin0);
  if (pt2.y > height){
    pt2.x = pt1.x - (r * cos0);
    pt2.y = pt1.y - (r * sin0);
  }  
  ofLine(pt1.x, pt1.y, pt2.x, pt2.y);
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
#pragma mark mouse

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::mousePressed( ofMouseEventArgs &e )
{
	if (isInsideRect(e.x, e.y, cameraView)){
		activeViewIndex = CAMERA_SOURCE_VIEW;
		cameraView.setActive();
		adjustedView.setActive(false);
		bgView.setActive(false);
		processedView.setActive(false);
		dataView.setActive(false);
	} else if (isInsideRect(e.x, e.y, adjustedView)){
		activeViewIndex = ADJUSTED_CAMERA_VIEW;
		adjustedView.setActive();
		cameraView.setActive(false);
		bgView.setActive(false);
		processedView.setActive(false);
		dataView.setActive(false);
	} else if (isInsideRect(e.x, e.y, bgView)){
		activeViewIndex = REFERENCE_BACKGROUND_VIEW;
		bgView.setActive();
		cameraView.setActive(false);
		adjustedView.setActive(false);
		processedView.setActive(false);
		dataView.setActive(false);
	} else if (isInsideRect(e.x, e.y, processedView)){
		activeViewIndex = PROCESSED_VIEW;
		processedView.setActive();
		cameraView.setActive(false);
		adjustedView.setActive(false);
		bgView.setActive(false);
		dataView.setActive(false);
	} else if (isInsideRect(e.x, e.y, dataView)){
		activeViewIndex = DATA_VIEW;
		dataView.setActive();
		cameraView.setActive(false);
		adjustedView.setActive(false);
		bgView.setActive(false);
		processedView.setActive(false);
	}
}

//---------------------------------------------------------------------------
bool ofxTSPSPeopleTracker::isInsideRect(float x, float y, ofRectangle rect){
    return ( x >= rect.x && x <= rect.x + rect.width && y >= rect.y && y <= rect.y + rect.height );
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
#pragma mark gui extension


//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::enableGuiEvents(){
    gui.enableEvents();
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::disableGuiEvents(){
    gui.disableEvents();    
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::addSlider(string name, int* value, int min, int max){
	//forward to the gui manager
	gui.addSlider(name, value, min, max);
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::addSlider(string name, float* value, float min, float max){
	gui.addSlider(name, value, min, max);	
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::addToggle(string name, bool* value){
	gui.addToggle(name, value);	
}

//---------------------------------------------------------------------------
guiTypeButton * ofxTSPSPeopleTracker::addExternalButton( string name, ofRectangle dimensions){
	return gui.addButton( name, dimensions );
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
#pragma mark accessors

/**
 * simple public getter for external classes
 */
//---------------------------------------------------------------------------
ofxTSPSPerson* ofxTSPSPeopleTracker::personAtIndex(int i) {
	return trackedPeople[i];
}

//---------------------------------------------------------------------------
int ofxTSPSPeopleTracker::totalPeople() {
	return trackedPeople.size();
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::enableHaarFeatures(bool doHaar){
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->bDetectHaar = doHaar;
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::enableOpticalFlow(bool doOpticalFlow){
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->bTrackOpticalFlow = doOpticalFlow;
}

//---------------------------------------------------------------------------
// for accessing the OSC sender whose parameters are adjusted in the GUI
ofxTSPSOscSender* ofxTSPSPeopleTracker::getOSCsender() {
	return &oscClient;
}

//---------------------------------------------------------------------------
ofxTSPSWebSocketSender * ofxTSPSPeopleTracker::getWebSocketServer(){
    return &webSocketServer;
}


//---------------------------------------------------------------------------
bool ofxTSPSPeopleTracker::useKinect(){
    if (p_Settings == NULL) p_Settings = gui.getSettings();
    return p_Settings->bUseKinect;
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setUseKinect( bool bUseKinect ){
    gui.setValueB( "USE_KINECT", bUseKinect );
    gui.update();
    if (p_Settings == NULL) p_Settings = gui.getSettings();
    p_Settings->bUseKinect = bUseKinect;
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
#pragma mark background management
//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::relearnBackground()
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->bLearnBackground = true;
}

//JG Disabled this feature
//void ofxTSPSPeopleTracker::enableBackgroundRelearnSmart(bool doSmartLearn)//auto-relearns if there are too many blobs in the scene
//{
//	p_Settings->bSmartLearnBackground = doSmartLearn;
//}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::enableBackgroundReleaernProgressive(bool doProgressive) //relearns over time using progessive frame averagering
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->bLearnBackgroundProgressive = doProgressive;
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setRelearnRate(float relearnRate)
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->fLearnRate = relearnRate;
}


//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
#pragma mark image control
//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setThreshold(float thresholdAmount)
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->threshold = thresholdAmount;
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setMinBlobSize(float minBlobSize)
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->minBlob = minBlobSize; 
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setMaxBlobSize(float maxBlobSize)
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->maxBlob = maxBlobSize;
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::enableSmooth(bool doSmooth)
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->bSmooth = doSmooth;
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setSmoothAmount(int smoothAmount)
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->smooth = smoothAmount;
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::enableHighpass(bool doHighpass)
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->bHighpass = doHighpass;
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setHighpassBlurAmount(int highpassBlurAmount)
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->highpassBlur = highpassBlurAmount;
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setHighpassNoiseAmount(int highpassNoiseAmount)
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->highpassNoise = highpassNoiseAmount;
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::enableAmplify(bool doAmp)
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->bAmplify = doAmp;
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setAmplifyAmount(int amplifyAmount)
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->highpassAmp = amplifyAmount;
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
#pragma mark filter controls
//haar
//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setHaarExpandArea(float haarExpandAmount) //makes the haar rect +area bigger
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->haarAreaPadding = haarExpandAmount;
}

//JG 1/21/10 disabled this feature to simplify the interface
//void ofxTSPSPeopleTracker::setMinHaarArea(float minArea)
//{
//	p_Settings->minHaarArea = minArea;
//}
//void ofxTSPSPeopleTracker::setMaxHaarArea(float maxArea)
//{
//	p_Settings->maxHaarArea = maxArea;
//}

//void ofxTSPSPeopleTracker::useHaarAsCentroid(bool useHaarCenter)
//{
//	p_Settings->bUseHaarAsCenter = useHaarCenter;
//}

//blobs
//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::enableFindHoles(bool findHoles)
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->bFindHoles = findHoles;
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::trackDarkBlobs()
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->trackType = TRACK_DARK;
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::trackLightBlobs()
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->trackType = TRACK_LIGHT;	
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setDrawMode(int mode)
{
	drawMode = mode;
}

//---------------------------------------------------------------------------
int ofxTSPSPeopleTracker::getDrawMode() 
{
	return drawMode;
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
#pragma mark gui customization

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setActiveView( int viewIndex ){
	int oldActiveView = activeViewIndex;
	activeViewIndex = viewIndex;
	
	if (activeViewIndex == CAMERA_SOURCE_VIEW){
		cameraView.setActive();
		adjustedView.setActive(false);
		bgView.setActive(false);
		processedView.setActive(false);
		dataView.setActive(false);
	} else if (activeViewIndex == ADJUSTED_CAMERA_VIEW){
		adjustedView.setActive();
		cameraView.setActive(false);
		bgView.setActive(false);
		processedView.setActive(false);
		dataView.setActive(false);
	} else if (activeViewIndex == REFERENCE_BACKGROUND_VIEW){
		bgView.setActive();
		cameraView.setActive(false);
		adjustedView.setActive(false);
		processedView.setActive(false);
		dataView.setActive(false);
	} else if (activeViewIndex == PROCESSED_VIEW){
		processedView.setActive();		cameraView.setActive(false);
		adjustedView.setActive(false);
		bgView.setActive(false);
		dataView.setActive(false);
	} else if (activeViewIndex == DATA_VIEW){
		dataView.setActive();
		cameraView.setActive(false);
		adjustedView.setActive(false);
		bgView.setActive(false);
		processedView.setActive(false);
	} else {
		activeViewIndex = oldActiveView;
	}
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setActiveDimensions ( int actWidth, int actHeight){
	activeWidth = actWidth;
	activeHeight = actHeight;
	updateViewRectangles();
}

//---------------------------------------------------------------------------
bool ofxTSPSPeopleTracker::loadFont( string fontName, int fontSize){
	bFontLoaded = font.loadFont(fontName, fontSize);
    if (bFontLoaded){
        cameraView.setFont(&font);
        adjustedView.setFont(&font);
        bgView.setFont(&font);
        processedView.setFont(&font);
        dataView.setFont(&font);
    }
    return bFontLoaded;
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setVideoGrabber(ofBaseVideo* grabber, tspsInputType inputType)
{
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	p_Settings->setVideoGrabber( grabber, inputType );
    if (inputType == TSPS_INPUT_VIDEO){
        gui.enableElement( "open video settings" );
        //gui.disableElement( "use kinect" );
    } else if (inputType == TSPS_INPUT_KINECT){
        gui.disableElement( "open video settings" );
        //gui.enableElement( "use kinect" );
    }
}

//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::updateViewRectangles(){
	//build all rectangles for drawing views
	ofPoint smallView;
	smallView.x = (activeWidth - GUI_WIDTH - spacing*6)/5.f;
	smallView.y = (height*TRACKING_SCALE_FACTOR) * (smallView.x/(width*TRACKING_SCALE_FACTOR));
	
	activeView.x = GUI_WIDTH + spacing;
	activeView.y = spacing;
	activeView.width = (activeWidth - GUI_WIDTH - spacing*2);
	activeView.height = (height*TRACKING_SCALE_FACTOR)*activeView.width/(width*TRACKING_SCALE_FACTOR);
	
	cameraView.x = GUI_WIDTH + spacing;
	cameraView.y = activeView.y + activeView.height + spacing;
	cameraView.width = smallView.x;
	cameraView.height = smallView.y;
	
	adjustedView.x = cameraView.x + cameraView.width + spacing;
	adjustedView.y = cameraView.y;
	adjustedView.width = smallView.x;
	adjustedView.height = smallView.y;
	
	bgView.x = adjustedView.x + adjustedView.width + spacing;
	bgView.y = cameraView.y;
	bgView.width = smallView.x;
	bgView.height = smallView.y;
	
	processedView.x = bgView.x + bgView.width + spacing;
	processedView.y = cameraView.y;
	processedView.width = smallView.x;
	processedView.height = smallView.y;
	
	dataView.x = processedView.x + processedView.width + spacing;
	dataView.y = cameraView.y;
	dataView.width = smallView.x;
	dataView.height = smallView.y;	
	//gui.drawQuadGui( activeView.x, activeView.y, activeView.width, activeView.height );
}

//---------------------------------------------------------------------------
bool ofxTSPSPeopleTracker::useVideoFile(){    
    if (p_Settings == NULL) p_Settings = gui.getSettings();
    return p_Settings->bUseVideoFile;
}


//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setUseVideoFile( bool bUseVideoFile ){
    if (p_Settings == NULL) p_Settings = gui.getSettings();
    p_Settings->bUseVideoFile = bUseVideoFile;
}

//---------------------------------------------------------------------------
string ofxTSPSPeopleTracker::getVideoFile(){
    if (p_Settings == NULL) p_Settings = gui.getSettings();
    return p_Settings->videoFile;    
}
//---------------------------------------------------------------------------
void ofxTSPSPeopleTracker::setVideoFile( string file ){
    if (p_Settings == NULL) p_Settings = gui.getSettings();
    p_Settings->videoFile = file;    
}

//---------------------------------------------------------------------------
// for accessing Optical Flow within a specific region
ofPoint ofxTSPSPeopleTracker::getOpticalFlowInRegion(float x, float y, float w, float h) {
	return opticalFlow.flowInRegion(x,y,w,h);
}


//---------------------------------------------------------------------------
// for accessing which view is the current view
bool ofxTSPSPeopleTracker::inCameraView() {
	return cameraView.isActive();
}

//---------------------------------------------------------------------------
bool ofxTSPSPeopleTracker::inBackgroundView() {
	return bgView.isActive();
}

//---------------------------------------------------------------------------
bool ofxTSPSPeopleTracker::inDifferencingView() {
	return processedView.isActive();
}

//---------------------------------------------------------------------------
bool ofxTSPSPeopleTracker::inDataView() {
	return dataView.isActive();
}

//---------------------------------------------------------------------------
bool ofxTSPSPeopleTracker::inAdjustedView() {
	return adjustedView.isActive();
}


// for getting a color version of the adjusted view image
// NOTE:  only works if the adjusted view is currently in color
//        (this parameter can be set in the GUI under the 'views' tab)
ofxCvColorImage ofxTSPSPeopleTracker::getAdjustedImageInColor() {
    if (p_Settings == NULL) p_Settings = gui.getSettings();
	if (p_Settings->bAdjustedViewInColor)
		return adjustedView.getColorImage();
}