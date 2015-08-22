/* 
 * File:   Projector.cpp
 * Author: Richard Greene
 * 
 * Encapsulates the functionality of the printer's projector.
 *
 * Created on May 30, 2014, 3:45 PM
 */

#include <iostream>

#include <SDL/SDL_image.h>
#include <Magick++.h>

#include <Projector.h>
#include <Logger.h>
#include <Filenames.h>
#include <MessageStrings.h>
#include <Settings.h>
#include <utils.h>

using namespace Magick;

#define ON  (true)
#define OFF (false)

/// Public constructor sets up SDL, base class tries to set up I2C connection 
Projector::Projector(unsigned char slaveAddress, int port) :
I2C_Device(slaveAddress, port),
_image(NULL)
{
    // see if we have an I2C connection to the projector
    _canControlViaI2C = (Read(PROJECTOR_HW_STATUS_REG) != ERROR_STATUS);
    if(!_canControlViaI2C)
        LOGGER.LogMessage(LOG_INFO, LOG_NO_PROJECTOR_I2C);

   // in case we exited abnormally before, 
   // tear down SDL before attempting to re-initialize it
   SDL_VideoQuit();
    
   if(SDL_Init(SDL_INIT_VIDEO) < 0)
   {
       LOGGER.LogError(LOG_ERR, errno, ERR_MSG(SdlInit), SDL_GetError());
       TearDownAndExit();  // we can't  run if we can't project images
   }
     
   // use the full screen to display the images
   const SDL_VideoInfo* videoInfo = SDL_GetVideoInfo();
#ifdef DEBUG
    // print out video parameters
    std::cout << "screen is " << videoInfo->current_w <<
                 " x "        << videoInfo->current_h <<
                 " x "        << (int)videoInfo->vfmt->BitsPerPixel << "bpp" <<
                 std::endl; 
#endif
   
   _screen = SDL_SetVideoMode (videoInfo->current_w, videoInfo->current_h, 
                               videoInfo->vfmt->BitsPerPixel, 
                               SDL_SWSURFACE | SDL_FULLSCREEN) ;   
   
   if(_screen == NULL)
   {
       LOGGER.LogError(LOG_ERR, errno, ERR_MSG(SdlSetMode), SDL_GetError());
       TearDownAndExit();  // we can't  run if we can't project images       
   }
   
    // hide the cursor
    SDL_ShowCursor(SDL_DISABLE);
    if(SDL_ShowCursor(SDL_QUERY) != SDL_DISABLE)
    {
        // not a fatal error
        LOGGER.LogError(LOG_WARNING, errno, ERR_MSG(SdlHideCursor), SDL_GetError());
    }
            
    ShowBlack();
    
    InitializeMagick("");
}

/// Destructor turns off projector and tears down SDL.
Projector::~Projector() 
{
    TearDown();
}

/// Set the image, ensuring release of the previous image.
void Projector::SetImage(SDL_Surface* image)
{
    SDL_FreeSurface(_image);
    _image = image;
}

/// Display the previously set image.
bool Projector::ShowImage()
{
    if(_image == NULL)
        return false;  // no image to display
    
    if(SDL_BlitSurface(_image, NULL, _screen, NULL) != 0)
    {
        return false;
    }
    
    TurnLED(ON);
    
    return SDL_Flip(_screen) == 0;    
}

/// Display an all black screen.
bool Projector::ShowBlack()
{
    TurnLED(OFF);

    if (SDL_MUSTLOCK(_screen) && SDL_LockSurface(_screen) != 0)
            return false;
    
    // fill the screen with black
    if(SDL_FillRect(_screen, NULL, 0) != 0)
        return false;
  
    if (SDL_MUSTLOCK(_screen))
        SDL_UnlockSurface (_screen) ;

    // display it
    return SDL_Flip(_screen) == 0;  
}

/// Turn off projector and tear down SDL
void Projector::TearDown()
{
    ShowBlack();
    SDL_FreeSurface(_image);
    SDL_VideoQuit();
    SDL_Quit();    
}

// Tear down projector and quit
void Projector::TearDownAndExit()
{
    TearDown();
    exit(-1);
}

/// Show a test pattern, to aid in focus and alignment.
void Projector::ShowTestPattern()
{
    SDL_FreeSurface(_image);
    
    _image = IMG_Load(TEST_PATTERN);
    if(_image == NULL)
    {
        LOGGER.LogError(LOG_WARNING, errno, ERR_MSG(LoadImageError), TEST_PATTERN);
    }    
    
    ShowImage();
}

/// Show a projector calibration image, to aid in alignment.
void Projector::ShowCalibrationPattern()
{
    SDL_FreeSurface(_image);
    
    _image = IMG_Load(CAL_IMAGE);
    if(_image == NULL)
    {
        LOGGER.LogError(LOG_WARNING, errno, ERR_MSG(LoadImageError), CAL_IMAGE);
    }    
    
    ShowImage();
}

/// Turn the projector's LED(s) on or off.  Set the current to the desired value 
/// first when turning them on.
void Projector::TurnLED(bool on)
{   
    if(!_canControlViaI2C)
        return;
    
    if(on)
    {
        // set the LED current, if we have a valid setting value for it
        int current = SETTINGS.GetInt(PROJECTOR_LED_CURRENT);
        if(current > 0)
        {
            // set the PWM polarity
            // though the PRO DLPC350 Programmer’s Guide says to set this after 
            // setting the LED currents, it appears to need to be set first
            // also, the Programmer’s Guide seems to have the polarity backwards
            unsigned char polarity = PROJECTOR_PWM_POLARITY_NORMAL;
            Write(PROJECTOR_LED_PWM_POLARITY_REG, &polarity, 1);
            
            unsigned char c = (unsigned char) current;
            // use the same value for all three LEDs
            unsigned char buf[3] = {c, c, c};

            Write(PROJECTOR_LED_CURRENT_REG, buf, 3);
        }
    }
    
    Write(PROJECTOR_LED_ENABLE_REG, on ? PROJECTOR_ENABLE_LEDS : 
                                         PROJECTOR_DISABLE_LEDS);
}

/// Scale the image by the given factor, and crop or pad back to full size.
void Projector::ScaleImage(SDL_Surface* surface, double scale, int layer)
{
#ifdef DEBUG
    StartStopwatch();
#endif    
    // Load image directly from PNG (temporarily done here, assuming .tar.gz data)
    char path[255];
    sprintf(path, "/var/smith/print_data/%s/slice_%d.png", SETTINGS.GetString(PRINT_FILE_SETTING).c_str(), layer);
    Image image;
    image.read(path);
    
    int origWidth  = (int) image.columns();
    int origHeight = (int) image.rows();

#ifdef DEBUG    
    std::cout << "creating image took " << StopStopwatch() << " ms" << std::endl; 
    StartStopwatch();
#endif
    
    // determine size of new image (rounding to nearest pixel)
    int resizeWidth =  (int)(origWidth * scale + 0.5);
    int resizeHeight = (int)(origHeight  * scale + 0.5);
    
    // scale the image  
    image.resize(Geometry(resizeWidth, resizeHeight));  
 
#ifdef DEBUG    
    std::cout << "resizing took " << StopStopwatch() << " ms" << std::endl; 
    StartStopwatch();

    // save a copy of the scaled image
//    image.write("/var/smith/resized.png"); 
#endif    
    
    if(scale < 1.0)
    {
        // pad the image back to full size
        image.extent(Geometry(origWidth, origHeight, 
                              (resizeWidth - origWidth) / 2, 
                              (resizeHeight - origHeight) / 2), "black");
    }
    else if (scale > 1.0)
    {
        // crop the image back to full size
        image.crop(Geometry(origWidth, origHeight, 
                            (resizeWidth - origWidth) / 2, 
                            (resizeHeight - origHeight) / 2));
    }

#ifdef DEBUG
    std::cout << "crop/pad " << StopStopwatch() << " ms" << std::endl; 
    StartStopwatch();
    
    // save a copy of the scaled & cropped or padded image
//    image.write("/var/smith/final.png"); 
#endif    
        
    // convert back to SDL_Surface
    image.write(0, 0, origWidth, origHeight, "G", CharPixel, surface->pixels);
    
#ifdef DEBUG
    std::cout << "conversion back to SDL took " << StopStopwatch() << " ms" << std::endl; 
#endif  
}
