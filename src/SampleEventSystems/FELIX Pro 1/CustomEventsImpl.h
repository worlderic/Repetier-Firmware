extern const char extzCalibGCode[] PROGMEM;
extern const char calibrationGCode[] PROGMEM;
extern const char removeBedGCode[] PROGMEM;
 
void Felix100MS() {
  if(PrintLine::linesCount == 0) {
    TemperatureController *bed = tempController[NUM_TEMPERATURE_LOOPS-1];
    if(bed->currentTemperatureC < MIN_DEFECT_TEMPERATURE) {
      bed->flags |= TEMPERATURE_CONTROLLER_FLAG_SENSDEFECT;
      if(!Printer::isAnyTempsensorDefect())
      {
          Printer::setAnyTempsensorDefect();
          reportTempsensorError();
          UI_MESSAGE(2); // show message
      }
    }
    
    // Test if bed is back
    if((bed->flags & TEMPERATURE_CONTROLLER_FLAG_SENSDEFECT) == TEMPERATURE_CONTROLLER_FLAG_SENSDEFECT && bed->currentTemperatureC > 0) {
      bed->flags &= ~TEMPERATURE_CONTROLLER_FLAG_SENSDEFECT;
      Printer::debugReset(8);
      Printer::unsetAnyTempsensorDefect();
      UI_RESET_MENU
    }
  }
}

bool probeValueNew = false;
bool probeValueOld;
char probeMessageOld[22];
bool changeFilWaitTarget = false;

void Felix500MS() {
  if(PrintLine::linesCount == 0) {
     Endstops::update(); // need to update endstops
     Endstops::update();
     probeValueNew = Endstops::zProbe();
     // check if something is changed with probe
     if (probeValueNew != probeValueOld){
        probeValueOld = probeValueNew;
        // probe is triggered, take action
        if(probeValueNew) {
          // store old message
          strncpy(probeMessageOld,uid.statusMsg,21);
          // make screen update with new message
          UI_STATUS_UPD_F(PSTR("Z sensor triggered!"));
        } else{ 
          //probe is not triggered;
          // restore old message
           UI_STATUS_RAM(probeMessageOld);
        }
     }
  }
  if(changeFilWaitTarget) {
    if(Extruder::current->tempControl.currentTemperatureC >= Extruder::current->tempControl.targetTemperatureC-2) {
      changeFilWaitTarget = false;
      uid.executeAction(UI_ACTION_WIZARD_FILAMENTCHANGE, true);   
    }
  }
}

void FelixContainCoordinates() {
  TemperatureController *bed = tempController[NUM_TEMPERATURE_LOOPS-1];
  if((bed->flags & TEMPERATURE_CONTROLLER_FLAG_SENSDEFECT) == TEMPERATURE_CONTROLLER_FLAG_SENSDEFECT) {
    Printer::destinationSteps[X_AXIS] = Printer::currentPositionSteps[X_AXIS];
    Printer::destinationSteps[Y_AXIS] = Printer::currentPositionSteps[Y_AXIS];
    Printer::destinationSteps[Z_AXIS] = Printer::currentPositionSteps[Z_AXIS];
    Printer::currentPositionSteps[E_AXIS] = Printer::destinationSteps[E_AXIS];  // to prevent fast e move when reactivated
    Printer::updateCurrentPosition(true);
  }
}
 
void cstmCooldown() {
   for(int i = 0;i < NUM_EXTRUDER; i++)
     Extruder::setTemperatureForExtruder(0, i);
#if HAVE_HEATED_BED
   Extruder::setHeatedBedTemperature(0);
#endif
} 
bool cstmIsHeating() {
  return extruder[0].tempControl.targetTemperatureC > 0 ||
    extruder[1].tempControl.targetTemperatureC > 0 ||
    heatedBedController.targetTemperatureC > 0; 
}
void setPreheatTemps(int16_t extr,int16_t bed,bool all,bool showMenu = true) {
  bool mod = false;
  mod |= heatedBedController.preheatTemperature != bed;
  heatedBedController.preheatTemperature = bed;
  if(all) {
     mod |= extruder[0].tempControl.preheatTemperature != extr;
     extruder[0].tempControl.preheatTemperature = extr;                    
     mod |= extruder[1].tempControl.preheatTemperature != extr;
     extruder[1].tempControl.preheatTemperature = extr;                    
  } else {
     mod |= Extruder::current->tempControl.preheatTemperature != extr;
     Extruder::current->tempControl.preheatTemperature = extr;                    
  }  
  if(mod) {
#if EEPROM_MODE != 0
      HAL::eprSetInt16(EPR_BED_PREHEAT_TEMP,heatedBedController.preheatTemperature);
      for(int i = 0; i < NUM_EXTRUDER;i++) {
         int o = i * EEPROM_EXTRUDER_LENGTH + EEPROM_EXTRUDER_OFFSET;
         Extruder *e = &extruder[i];
         HAL::eprSetInt16(o+EPR_EXTRUDER_PREHEAT,e->tempControl.preheatTemperature);
      }            
      EEPROM::updateChecksum();
#endif                    
  }
  if(showMenu)
    uid.pushMenu(&ui_menu_preheatinfo,true);
}

// Preheat to preselected temperature for active extruder and continue to
// filamentchange when finished.
void preheatFCActive() {
  
  Extruder::setTemperatureForExtruder(Extruder::current->tempControl.preheatTemperature,Extruder::current->id,false);
  changeFilWaitTarget = true;
  uid.popMenu(false);
  uid.pushMenu(&ui_menu_ch3,true);
}

#ifdef HALFAUTOMATIC_LEVELING
// Measure fixed point height and P1
void halfautomaticLevel2() {
  uid.popMenu(false);
  uid.pushMenu(&cui_msg_measuring,true);
  PlaneBuilder planeBuilder;
  Printer::moveToReal(HALF_FIX_X, HALF_FIX_Y, IGNORE_COORDINATE, IGNORE_COORDINATE, EXTRUDER_SWITCH_XY_SPEED);
  Commands::waitUntilEndOfAllMoves();
  float halfRefHeight = Printer::runZProbe(false, false); 
  planeBuilder.addPoint(HALF_FIX_X, HALF_FIX_Y, halfRefHeight);
  Printer::moveToReal(HALF_P1_X, HALF_P1_Y, IGNORE_COORDINATE, IGNORE_COORDINATE, EXTRUDER_SWITCH_XY_SPEED);
  Commands::waitUntilEndOfAllMoves();
  float p1 = Printer::runZProbe(false, false);
  planeBuilder.addPoint(HALF_P1_X, HALF_P1_Y, p1); 
  Printer::moveToReal(HALF_P2_X, HALF_P2_Y, IGNORE_COORDINATE, IGNORE_COORDINATE, EXTRUDER_SWITCH_XY_SPEED);
  Commands::waitUntilEndOfAllMoves();
  float p2 = Printer::runZProbe(false, false);
  planeBuilder.addPoint(HALF_P2_X, HALF_P2_Y, p2);
  Plane plane;
  planeBuilder.createPlane(plane);
  // float z1 = p1 + (p2 - p1) / (HALF_P2_Y - HALF_P1_Y) * (HALF_WHEEL_P1 - HALF_P1_Y) - halfRefHeight; 
  // float z2 = p1 + (p2 - p1) / (HALF_P2_Y - HALF_P1_Y) * (HALF_WHEEL_P2 - HALF_P1_Y) - halfRefHeight;
  float z1 =(plane.z(HALF_P1_X, HALF_WHEEL_P1) - halfRefHeight) * 360 / HALF_PITCH;
  float z2 = (plane.z(HALF_P1_X, HALF_WHEEL_P2) - halfRefHeight) * 360 / HALF_PITCH;
  Printer::wizardStack[0].f = z2; 
  Printer::wizardStack[1].f = z1; 
  uid.popMenu(false);
  if(fabs(z1) <= 10 && fabs(z2) <= 10) {
    Printer::finishProbing();
    uid.pushMenu(&cui_calib_zprobe_succ, true);
  } else {
    uid.pushMenu(&ui_half_show,true);
  }
}
// Finish leveling
void halfautomaticLevel3() {
  Printer::finishProbing();
  uid.popMenu(false);
}
/* Start autoleveling */
void halfautomaticLevel1() {
  uid.pushMenu(&cui_msg_measuring,true);
  Printer::homeAxis(true, true, true);
  Printer::moveToReal(IGNORE_COORDINATE, IGNORE_COORDINATE, HALF_Z, IGNORE_COORDINATE, Printer::homingFeedrate[Z_AXIS]);
  Printer::startProbing(true);
  uid.popMenu(false);
  halfautomaticLevel2();  
}
#endif

#ifdef ZPROBE_HEIGHT_ROUTINE
/*
- Click on function
- Message preparing
- Home, go to P1 from z leveling
- Probe position for reference height.
- Go to your preset z value.
- Message to adjust hight with wheel
Change Z with wheel
until card fits with
no force below and
press the button
- Compute new z probe height from this.
- Back to menu.


*/
float refZ;
void cZPHeight1() {
  uid.pushMenu(&cui_msg_preparing,true);
  Printer::homeAxis(true, true, true);
  Printer::moveToReal(IGNORE_COORDINATE, IGNORE_COORDINATE, EEPROM::zProbeBedDistance(), IGNORE_COORDINATE, Printer::homingFeedrate[Z_AXIS]);
  Printer::moveToReal(HALF_FIX_X, HALF_FIX_Y, IGNORE_COORDINATE, IGNORE_COORDINATE, EXTRUDER_SWITCH_XY_SPEED);
  //refZ = Printer::runZProbe(true, true) - EEPROM::zProbeBedDistance() - Printer::zBedOffset;
  refZ = 0;
  Com::printF(PSTR(" cur:"),Printer::currentPosition[Z_AXIS],3);Com::printF(PSTR(" refZ:"), refZ, 3);Com::printFLN(PSTR(" atZ:"), EEPROM::zProbeBedDistance(), 3);
  Printer::moveToReal(IGNORE_COORDINATE, IGNORE_COORDINATE, ZPROBE_REF_HEIGHT - refZ, IGNORE_COORDINATE, Printer::homingFeedrate[Z_AXIS]);
  Printer::updateCurrentPosition(true);
  uid.popMenu(false);
  uid.pushMenu(&cui_calib_zprobe_info, true);
}
void cZPHeight2() {
#if FEATURE_Z_PROBE
  // float diff = refZ + Printer::currentPosition[Z_AXIS] - ZPROBE_REF_HEIGHT;
  Commands::printCurrentPosition();
  float diff = (Printer::lastCmdPos[Z_AXIS] - refZ) - (ZPROBE_REF_HEIGHT - refZ);
  Com::printF(PSTR("oldZPH:"),EEPROM::zProbeHeight(),3);Com::printF(PSTR(" diff:"), diff,3);Com::printF(PSTR(" cur:"),Printer::currentPosition[Z_AXIS],3); Com::printFLN(PSTR(" REFH:"),(float)ZPROBE_REF_HEIGHT, 2);
  if(diff > 1) diff = 1;
  if(diff < -1) diff = -1;
  Printer::currentPositionSteps[Z_AXIS] = ZPROBE_REF_HEIGHT * Printer::axisStepsPerMM[Z_AXIS];
  Printer::updateCurrentPosition(true);
	float zProbeHeight = EEPROM::zProbeHeight() - diff;    
#if EEPROM_MODE != 0 // Com::tZProbeHeight is not declared when EEPROM_MODE is 0
	EEPROM::setZProbeHeight(zProbeHeight); // will also report on output
#else
	Com::printFLN(PSTR("Z-probe height [mm]:"), zProbeHeight);
#endif
  uid.popMenu(false);
  uid.menuLevel = 0;
  UI_STATUS_UPD("Probe calibrated");
#endif
}
#endif


bool cCustomParser(char c1, char c2) {
  if(c1 == 'b') {
    if(c2 == 'a') {
      float x = Printer::wizardStack[0].f;
      uid.addFloat(fabs(x),0,0);
      uid.addChar(2);
      uid.addChar(x < 0 ? 'R' : 'L');
      return true;
    }
    if(c2 == 'b') {
      float x = Printer::wizardStack[1].f;
      uid.addFloat(fabs(x),0,0);
      uid.addChar(2);
      uid.addChar(x < 0 ? 'R' : 'L');
      return true;
    }
  }
  return false;
}

bool cExecuteOverride(int action,bool allowMoves) {
  switch(action) {
    case UI_ACTION_LOAD_EEPROM:
      EEPROM::readDataFromEEPROM(true);
      EEPROM::storeDataIntoEEPROM(false);
      Extruder::selectExtruderById(Extruder::current->id);
      uid.pushMenu(&ui_menu_eeprom_loaded, false);
      BEEP_LONG;
      return true;
#if FEATURE_AUTOLEVEL & FEATURE_Z_PROBE
    case UI_ACTION_AUTOLEVEL2:
      uid.popMenu(false);
      uid.pushMenu(&ui_msg_calibrating_bed, true);
      Printer::homeAxis(true, true, true);
      Printer::moveToReal(IGNORE_COORDINATE, IGNORE_COORDINATE, 3, IGNORE_COORDINATE, Printer::homingFeedrate[Z_AXIS]);
      runBedLeveling(2);
      Extruder::disableAllHeater();
      uid.popMenu(true);
      return true;
#endif      
  }
  return false;
}

void cExecute(int action,bool allowMoves) {
  switch(action) {
#ifdef HALFAUTOMATIC_LEVELING
  case UI_ACTION_HALFAUTO_LEV:
    halfautomaticLevel1();
    break;
  case UI_ACTION_HALFAUTO_LEV2:
    halfautomaticLevel2();
    break;
  case UI_ACTION_HALFAUTO_LEV3:
    halfautomaticLevel3();
    break;
#endif  
  case UI_ACTION_XY1_BACK:
     uid.popMenu(true);
     break;
  case UI_ACTION_XY1_CONT:
    uid.popMenu(false);
    uid.pushMenu(&ui_exy2,true);
    break;
  case UI_ACTION_XY2_BACK:
    uid.popMenu(true);
    break;
  case UI_ACTION_XY2_CONT:
    uid.popMenu(false);
    uid.pushMenu(&ui_msg_printxycal,true);
    flashSource.executeCommands(calibrationGCode,false,UI_ACTION_CALEX_XY3);
    break;
  case UI_ACTION_CALEX_XY3:
    Printer::wizardStackPos = 0;
    Printer::wizardStack[0].l = 5;
    Printer::wizardStack[1].l = 5;  
    uid.popMenu(false);
    uid.pushMenu(&ui_exy3,true);
    break;
  case UI_ACTION_EXY_STORE:
    {
      int32_t xcor = static_cast<int32_t>((Printer::axisStepsPerMM[X_AXIS] * (Printer::wizardStack[0].l - 5)) / 10);
      int32_t ycor = static_cast<int32_t>((Printer::axisStepsPerMM[Y_AXIS] * (Printer::wizardStack[1].l - 5)) / 10);
      extruder[1].xOffset -= xcor;
      extruder[1].yOffset += ycor;
      if(xcor != 0 || ycor != 0)
        EEPROM::storeDataIntoEEPROM(false);
      uid.popMenu(true);  
    }
    break;
  case UI_ACTION_CALEX:
    uid.pushMenu(&ui_calextr_sub,true);
    break;
  case UI_ACTION_CALEX_XY:
    Printer::wizardStackPos = 0;
    Printer::wizardStack[0].l = 5;
    Printer::wizardStack[1].l = 5;
    uid.pushMenu(&ui_exy1,true);
    break;
  case UI_ACTION_CALEX_Z:
    uid.pushMenu(&ui_msg_clearbed_zcalib,true);
    break;
  case UI_ACTION_CALEX_Z3:
    uid.popMenu(true);
    break;
  case UI_ACTION_PRECOOL1:
    if(cstmIsHeating()) {
      cstmCooldown();
    } else {
      Extruder::setHeatedBedTemperature(heatedBedController.preheatTemperature);
      Extruder::setTemperatureForExtruder(extruder[0].tempControl.preheatTemperature,0,false);
      Extruder::setTemperatureForExtruder(0,1,false);
    }
    break;    
  case UI_ACTION_PRECOOL2:
    if(cstmIsHeating()) {
      cstmCooldown();
    } else {
      Extruder::setHeatedBedTemperature(heatedBedController.preheatTemperature);
      Extruder::setTemperatureForExtruder(extruder[0].tempControl.preheatTemperature,0,false);
      Extruder::setTemperatureForExtruder(extruder[1].tempControl.preheatTemperature,1,false);
    }
    break;
  case UI_ACTION_REMOVEBED: {
    flashSource.executeCommands(removeBedGCode,false,0);
    /* TemperatureController *bed = tempController[NUM_TEMPERATURE_LOOPS-1];
    Extruder::setHeatedBedTemperature(0);
    if(!Printer::isZHomed())
      Printer::homeAxis(true,true,true);
    else
      Printer::homeAxis(false,true,false);  
    Printer::moveToReal(IGNORE_COORDINATE,IGNORE_COORDINATE, 200, IGNORE_COORDINATE, 30,false); */
    }      
    break;
  case UI_ACTION_SPH_PLA_ACTIVE: // 190/55
    setPreheatTemps(190, 55, false);
    break;
  case UI_ACTION_SPH_PETG_ACTIVE:
    setPreheatTemps(215, 70, false);
    break;
  case UI_ACTION_SPH_PVA_ACTIVE:
    setPreheatTemps(185, 55, false);
    break;
  case UI_ACTION_SPH_FLEX_ACTIVE:
    setPreheatTemps(185, 40, false);
    break;
  case UI_ACTION_SPH_ABS_ACTIVE:
    setPreheatTemps(225, 85, false);
    break;
  case UI_ACTION_SPH_GLASS_ACTIVE:
    setPreheatTemps(225, 85, false);
    break;
  case UI_ACTION_SPH_WOOD_ACTIVE:
    setPreheatTemps(185, 55, false);
    break;
  case UI_ACTION_SPH_PLA_ALL:
    setPreheatTemps(190, 55, true);
    break;
  case UI_ACTION_SPH_PETG_ALL:
    setPreheatTemps(215, 70, true);
    break;
  case UI_ACTION_SPH_PVA_ALL:
    setPreheatTemps(185, 55, true);
    break;
  case UI_ACTION_SPH_FLEX_ALL:
    setPreheatTemps(185, 40, true);
    break;
  case UI_ACTION_SPH_ABS_ALL:
    setPreheatTemps(225, 85, true);
    break;
  case UI_ACTION_SPH_GLASS_ALL:
    setPreheatTemps(225, 85, true);
    break;
  case UI_ACTION_SPH_WOOD_ALL:
    setPreheatTemps(185, 55, true);
    break;
  // Filament change related functions  
  case UI_ACTION_FC_SELECT1:
    Extruder::selectExtruderById(0);
    uid.popMenu(false);
    uid.pushMenu(&ui_menu_ch2,true);
    break;
  case UI_ACTION_FC_SELECT2:
    Extruder::selectExtruderById(1);
    uid.popMenu(false);
    uid.pushMenu(&ui_menu_ch2,true);
    break;
  case UI_ACTION_FC_PLA:
    setPreheatTemps(190, 55, false, false);
    preheatFCActive();
    break;
  case UI_ACTION_FC_PETG:
    setPreheatTemps(215, 70, false, false);
    preheatFCActive();
    break;
  case UI_ACTION_FC_PVA:
    setPreheatTemps(210, 55, false, false);
    preheatFCActive();
    break;
  case UI_ACTION_FC_FLEX:
    setPreheatTemps(185, 40, false, false);
    preheatFCActive();
    break;
  case UI_ACTION_FC_ABS:
    setPreheatTemps(225, 85, false, false);
    preheatFCActive();
    break;
  case UI_ACTION_FC_GLASS:
    setPreheatTemps(225, 85, false, false);
    preheatFCActive();
    break;
  case UI_ACTION_FC_WOOD:
    setPreheatTemps(185, 55, false, false);
    preheatFCActive();
    break;
  case UI_ACTION_FC_CUSTOM:
    Printer::wizardStack[0].l = 190;
    uid.popMenu(false);
    uid.pushMenu(&ui_menu_ch2a,true);
    break;
  case UI_ACTION_FC_WAITHEAT:
    changeFilWaitTarget = false;
    Extruder::setTemperatureForExtruder(0,Extruder::current->id,false);
    uid.popMenu(true);
    break;            
  case UI_ACTION_FC_BACK1:
    uid.popMenu(false);
    uid.pushMenu(&ui_menu_chf,true);
    break;
  case UI_ACTION_FC_BACK2:
    uid.popMenu(false);
    uid.pushMenu(&ui_menu_ch2,true);
    break;
#ifdef ZPROBE_HEIGHT_ROUTINE    
  case UI_ACTION_START_CZREFH:
    cZPHeight1();
    break;
#endif        
  }
}

void cNextPrevious(int action,bool allowMoves,int increment) {
  switch(action) {
  case UI_ACTION_EXY_XOFFSET:
    Printer::wizardStack[0].l += increment;
    if(Printer::wizardStack[0].l < 1)
      Printer::wizardStack[0].l = 1;
    if(Printer::wizardStack[0].l > 9)
      Printer::wizardStack[0].l = 9;
    break;
  case UI_ACTION_EXY_YOFFSET:
    Printer::wizardStack[1].l += increment;
    if(Printer::wizardStack[1].l < 1)
      Printer::wizardStack[1].l = 1;
    if(Printer::wizardStack[1].l > 9)
      Printer::wizardStack[1].l = 9;
    break;
  case UI_ACTION_FC_CUSTOM_SET:
    Printer::wizardStack[0].l += increment;
    if(Printer::wizardStack[0].l < 150)
      Printer::wizardStack[0].l = 150;
    if(Printer::wizardStack[0].l > 275)
      Printer::wizardStack[0].l = 275;
    break;
  case UI_ACTION_CZREFH: {
      bool old = Printer::isNoDestinationCheck();
      Printer::setNoDestinationCheck(true);
      PrintLine::moveRelativeDistanceInStepsReal(0, 0, ((long)increment * Printer::axisStepsPerMM[Z_AXIS]) / 100, 0, Printer::homingFeedrate[Z_AXIS],false,false);
      Printer::setNoDestinationCheck(old);
    }
    break;   
  }
}
 
void cOkWizard(int action) {
  switch(action) {
  case UI_ACTION_CZREFH_SUCC:
    uid.menuLevel = 0;
    break;
  case UI_ACTION_CALEX_Z2:
     uid.popMenu(false);
     uid.pushMenu(&ui_msg_extzcalib,true);
     flashSource.executeCommands(extzCalibGCode,false,UI_ACTION_CALEX_Z3);
     break;
  case UI_ACTION_FC_CUSTOM_SET:
    setPreheatTemps(Printer::wizardStack[0].l, 55, false, false);
    preheatFCActive();
    break;
#ifdef ZPROBE_HEIGHT_ROUTINE    
  case UI_ACTION_CZREFH_INFO:
      uid.popMenu(false);
      uid.pushMenu(&cui_calib_zprobe, true);
      break;
  case UI_ACTION_CZREFH:
    cZPHeight2();
    break;
#endif    
  }
} 

void cRelaxExtruderEndstop() {
#ifndef NO_RELAX_ENDSTOPS
  uint8_t oldJam = Printer::isJamcontrolDisabled();
  Printer::setJamcontrolDisabled(true); // prevent jam message when no filament is inserted 
  int activeExtruder = Extruder::current->id;
  Printer::setColdExtrusionAllowed(true);
  Printer::destinationSteps[E_AXIS] = Printer::currentPositionSteps[E_AXIS] = 0;
  Printer::moveToReal(IGNORE_COORDINATE,IGNORE_COORDINATE,IGNORE_COORDINATE,0.25,10);
  Extruder::selectExtruderById(1 - activeExtruder);
  Printer::destinationSteps[E_AXIS] = Printer::currentPositionSteps[E_AXIS] = 0;
  Printer::moveToReal(IGNORE_COORDINATE,IGNORE_COORDINATE,IGNORE_COORDINATE,0.25,10);
  Printer::setColdExtrusionAllowed(false);
  Extruder::selectExtruderById(activeExtruder);
  Printer::destinationSteps[E_AXIS] = Printer::currentPositionSteps[E_AXIS] = 0;
  Printer::setJamcontrolDisabled(oldJam);
#endif  
}

bool cRefreshPage() {
  if(uid.menuLevel != 0)
    return false;
    if(uid.menuPos[0] == 0 && Printer::isPrinting())
      return false;
  // Use big chars, skip 5th line
      uint16_t r;
  uint8_t mtype = UI_MENU_TYPE_INFO;
  char cache[UI_ROWS+UI_ROWS_EXTRA][MAX_COLS + 1];
  uid.adjustMenuPos();
#if defined(UI_HEAD) && UI_DISPLAY_TYPE == DISPLAY_U8G
  char head[MAX_COLS + 1];
  uid.col = 0;
  uid.parse(uiHead,false);
  strcpy(head,uid.printCols);
#endif
  char *text;

  UIMenu *men = (UIMenu*)pgm_read_word(&(ui_pages[uid.menuPos[0]]));
  uint16_t nr = pgm_read_word_near(&(men->numEntries));
  UIMenuEntry **entries = (UIMenuEntry**)pgm_read_word(&(men->entries));
  for(r = 0; r < nr && r < UI_ROWS + UI_ROWS_EXTRA; r++) {
    UIMenuEntry *ent = (UIMenuEntry *)pgm_read_word(&(entries[r]));
    uid.col = 0;
    text = (char*)pgm_read_word(&(ent->text));
    if(text == NULL)
       text = (char*)Com::translatedF(pgm_read_word(&(ent->translation)));
    uid.parse(text,false);
    strcpy(cache[r],uid.printCols);
  }

  uid.printCols[0] = 0;
  while(r < UI_ROWS) // delete trailing empty rows
    strcpy(cache[r++],uid.printCols);
    // compute line scrolling values    
    uint8_t off0 = (uid.shift <= 0 ? 0 : uid.shift), y;
    uint8_t off[UI_ROWS + UI_ROWS_EXTRA];
    for(y = 0; y < UI_ROWS + UI_ROWS_EXTRA; y++)
    {
        uint8_t len = strlen(cache[y]); // length of line content
        off[y] = len > UI_COLS ? RMath::min(len - UI_COLS,off0) : 0;
        if(len > UI_COLS)
        {
            off[y] = RMath::min(len - UI_COLS,off0);
        }
        else off[y] = 0;
     }
#if UI_DISPLAY_TYPE == DISPLAY_U8G
#define drawHProgressBar(x,y,width,height,progress) \
     {u8g_DrawFrame(&u8g,x,y, width, height);  \
     int p = ceil((width-2) * progress / 100); \
     u8g_DrawBox(&u8g,x+1,y+1, p, height-2);}


#define drawVProgressBar(x,y,width,height,progress) \
     {u8g_DrawFrame(&u8g,x,y, width, height);  \
     int p = height-1 - ceil((height-2) * progress / 100); \
     u8g_DrawBox(&u8g,x+1,y+p, width-2, (height-p));}

        //u8g picture loop
        u8g_FirstPage(&u8g);
        do
        {

#endif //  UI_DISPLAY_TYPE == DISPLAY_U8G
#if defined(UI_HEAD) && UI_DISPLAY_TYPE == DISPLAY_U8G
					// Show status line
					u8g_SetColorIndex(&u8g,1);
					u8g_draw_box(&u8g, 0, 0, u8g_GetWidth(&u8g), UI_FONT_SMALL_HEIGHT + 1);
					u8g_SetColorIndex(&u8g, 0);
					u8g_SetFont(&u8g,UI_FONT_SMALL);
                    if(u8g_IsBBXIntersection(&u8g, 0, 1, 1, UI_FONT_SMALL_HEIGHT+1))
						printU8GRow(1,UI_FONT_SMALL_HEIGHT,head);
					u8g_SetFont(&u8g, UI_FONT_DEFAULT);		
					u8g_SetColorIndex(&u8g,1);

#endif
           for(y = 0; y < UI_ROWS + UI_ROWS_EXTRA; y++) {
             uint8_t l = y;
             if(y == 4)
                continue;
             if(y == 5)
              l = 4;   
             uid.printRow(l, &cache[y][off[y]], NULL, UI_COLS);
           }
#if UI_DISPLAY_TYPE == DISPLAY_U8G
        }
        while( u8g_NextPage(&u8g) );  //end picture loop
#endif
  Printer::toggleAnimation();    
  return true;
}

FSTRINGVALUE(removeBedGCode,
"M140 S0\n"
"G28 Y0\n"
"M84\n"
);

FSTRINGVALUE(extzCalibGCode,
"M104 S190 T0\n"
"M104 S190 T1\n"
"M109 S190 T0\n"
"M109 S190 T1\n"
"G28\n"
"G1 X137 Y45 Z10 F9000\n"
"G134 P0 S1\n" //G134 Px Sx Zx - Calibrate nozzle height difference (need z probe in nozzle!) Px = reference extruder, Sx = only measure extrude x against reference, Zx = add to measured z distance for Sx for correction.
"M104 S0 T0\n"
"M104 S0 T1\n"
"M400"
);

 FSTRINGVALUE(calibrationGCode,
/* "M140 S55\n"
"M104 T0 S190\n"
"M104 T1 S140\n"
"M117 Homing\n"
"G91\n"
"G1 Z5 F7800\n"
"G90\n"
"M302 S1\n"
"T1\n"
"G92 E0\n"
"G1 E0.25 F500\n"
"T0\n"
"G92 E0\n"
"G1 E0.25 F500\n"
"M302 S0\n"
"G28 Z\n"
"M190 S55\n"
"T0\n"
"M109 S190\n"
"G1 X236.0 Y243.2 Z5.0 F7800.0\n"
"G1 X236.0 Y243.2 Z0.3 F7800.0\n"
"G1 X108.5 Y243.2 Z0.3 F1500.0 E15\n"
"G1 E14.5 F3000\n"
"G92 E0\n"*/
"M117 FELIXprinting...\n"
"G21\n"
"G90\n"
"M82\n"
"G92 E0\n"
"G1 E0.2 F3000\n"
"T0\n"
"G92 E0\n"
"G1 Z0.300 F7800.000\n"
"G1 X123.501 Y191.179 F7800.000\n"
"G1 Z0.250 F7800.000\n"
"G1 E0.75000 F3000.00000\n"
"G1 X123.740 Y191.108 E0.76270 F1500.000\n"
"G1 X126.694 Y191.121 E0.91370\n"
"G1 X126.760 Y191.199 E0.91893\n"
"G1 X130.789 Y197.392 E1.29651\n"
"G1 X130.956 Y197.542 E1.30799\n"
"G1 X131.124 Y197.393 E1.31948\n"
"G1 X135.163 Y191.199 E1.69737\n"
"G1 X135.229 Y191.121 E1.70260\n"
"G1 X138.187 Y191.108 E1.85375\n"
"G1 X138.425 Y191.179 E1.86646\n"
"G1 X138.354 Y191.418 E1.87916\n"
"G1 X132.693 Y200.098 E2.40883\n"
"G1 X132.642 Y200.208 E2.41497\n"
"G1 X132.692 Y200.317 E2.42111\n"
"G1 X137.955 Y208.399 E2.91402\n"
"G1 X138.025 Y208.638 E2.92672\n"
"G1 X137.787 Y208.708 E2.93942\n"
"G1 X134.833 Y208.696 E3.09042\n"
"G1 X134.766 Y208.618 E3.09565\n"
"G1 X131.124 Y203.019 E3.43699\n"
"G1 X130.956 Y202.870 E3.44847\n"
"G1 X130.789 Y203.019 E3.45996\n"
"G1 X127.139 Y208.618 E3.80151\n"
"G1 X127.073 Y208.696 E3.80674\n"
"G1 X124.115 Y208.708 E3.95790\n"
"G1 X123.877 Y208.638 E3.97060\n"
"G1 X123.948 Y208.399 E3.98330\n"
"G1 X129.220 Y200.313 E4.47664\n"
"G1 X129.270 Y200.204 E4.48278\n"
"G1 X129.220 Y200.095 E4.48892\n"
"G1 X123.572 Y191.418 E5.01807\n"
"G1 X123.501 Y191.179 E5.03077\n"
"G1 E4.28077 F3000.00000\n"
"G92 E0\n"
"G1 Z0.550 F7800.000\n"
"G1 X99.175 Y191.713 F7800.000\n"
"G1 Z0.250 F7800.000\n"
"G1 E0.75000 F3000.00000\n"
"G1 X99.417 Y191.755 E0.76253 F1500.000\n"
"G1 X99.965 Y192.071 E0.79488\n"
"G1 X100.500 Y192.468 E0.82893\n"
"G1 X100.972 Y192.912 E0.86205\n"
"G1 X101.219 Y193.035 E0.87615\n"
"G1 X101.310 Y192.766 E0.89064\n"
"G1 X101.310 Y186.758 E1.19767\n"
"G1 X101.344 Y186.593 E1.20632\n"
"G1 X101.510 Y186.558 E1.21497\n"
"G1 X102.440 Y186.558 E1.26254\n"
"G1 X102.606 Y186.593 E1.27118\n"
"G1 X102.640 Y186.758 E1.27983\n"
"G1 X102.640 Y194.958 E1.69892\n"
"G1 X102.606 Y195.124 E1.70756\n"
"G1 X102.440 Y195.158 E1.71621\n"
"G1 X101.723 Y195.158 E1.75290\n"
"G1 X101.598 Y195.139 E1.75935\n"
"G1 X101.379 Y194.741 E1.78254\n"
"G1 X101.156 Y194.409 E1.80300\n"
"G1 X100.951 Y194.167 E1.81924\n"
"G1 X100.704 Y193.920 E1.83707\n"
"G1 X100.328 Y193.612 E1.86192\n"
"G1 X99.965 Y193.359 E1.88453\n"
"G1 X99.598 Y193.137 E1.90644\n"
"G1 X99.138 Y192.904 E1.93280\n"
"G1 X99.117 Y192.777 E1.93939\n"
"G1 X99.117 Y191.928 E1.98278\n"
"G1 X99.175 Y191.713 E1.99415\n"
"G1 E1.24415 F3000.00000\n"
"G92 E0\n"
"G1 Z0.550 F7800.000\n"
"G1 X90.079 Y100.513 F7800.000\n"
"G1 Z0.250 F7800.000\n"
"G1 E0.75000 F3000.00000\n"
"G1 X90.321 Y100.555 E0.76253 F1500.000\n"
"G1 X90.869 Y100.871 E0.79488\n"
"G1 X91.404 Y101.268 E0.82893\n"
"G1 X91.876 Y101.712 E0.86205\n"
"G1 X92.123 Y101.835 E0.87615\n"
"G1 X92.214 Y101.566 E0.89066\n"
"G1 X92.214 Y95.558 E1.19769\n"
"G1 X92.248 Y95.393 E1.20634\n"
"G1 X92.414 Y95.358 E1.21498\n"
"G1 X93.344 Y95.358 E1.26255\n"
"G1 X93.510 Y95.393 E1.27120\n"
"G1 X93.544 Y95.558 E1.27985\n"
"G1 X93.544 Y103.758 E1.69893\n"
"G1 X93.510 Y103.924 E1.70758\n"
"G1 X93.344 Y103.958 E1.71623\n"
"G1 X92.627 Y103.958 E1.75291\n"
"G1 X92.502 Y103.939 E1.75937\n"
"G1 X92.283 Y103.541 E1.78256\n"
"G1 X92.060 Y103.209 E1.80302\n"
"G1 X91.855 Y102.967 E1.81926\n"
"G1 X91.608 Y102.720 E1.83709\n"
"G1 X91.231 Y102.412 E1.86193\n"
"G1 X90.869 Y102.159 E1.88454\n"
"G1 X90.502 Y101.937 E1.90646\n"
"G1 X90.042 Y101.704 E1.93282\n"
"G1 X90.021 Y101.577 E1.93941\n"
"G1 X90.021 Y100.728 E1.98279\n"
"G1 X90.079 Y100.513 E1.99417\n"
"G1 E1.24417 F3000.00000\n"
"G92 E0\n"
"G1 Z0.550 F7800.000\n"
"G1 X74.622 Y78.168 F7800.000\n"
"G1 Z0.250 F7800.000\n"
"G1 E0.75000 F3000.00000\n"
"G1 X79.538 Y68.501 E1.30423 F1500.000\n"
"G1 X79.540 Y61.058 E1.68462\n"
"G1 X79.575 Y60.893 E1.69327\n"
"G1 X79.740 Y60.858 E1.70192\n"
"G1 X82.171 Y60.858 E1.82615\n"
"G1 X82.337 Y60.893 E1.83480\n"
"G1 X82.371 Y61.058 E1.84344\n"
"G1 X82.374 Y68.501 E2.22384\n"
"G1 X87.289 Y78.168 E2.77807\n"
"G1 X87.335 Y78.396 E2.78995\n"
"G1 X87.111 Y78.458 E2.80184\n"
"G1 X84.414 Y78.458 E2.93969\n"
"G1 X84.301 Y78.443 E2.94552\n"
"G1 X84.236 Y78.349 E2.95134\n"
"G1 X81.134 Y72.254 E3.30086\n"
"G1 X80.956 Y72.070 E3.31397\n"
"G1 X80.778 Y72.254 E3.32708\n"
"G1 X77.676 Y78.349 E3.67660\n"
"G1 X77.611 Y78.443 E3.68243\n"
"G1 X77.498 Y78.458 E3.68825\n"
"G1 X74.800 Y78.458 E3.82611\n"
"G1 X74.576 Y78.396 E3.83799\n"
"G1 X74.622 Y78.168 E3.84988\n"
"G1 E3.09988 F3000.00000\n"
"G92 E0\n"
"G1 Z0.550 F7800.000\n"
"G1 X90.935 Y41.195 F7800.000\n"
"G1 Z0.250 F7800.000\n"
"G1 E0.75000 F3000.00000\n"
"G1 X90.959 Y40.816 E0.76943 F1500.000\n"
"G1 X91.010 Y40.523 E0.78460\n"
"G1 X91.145 Y40.139 E0.80541\n"
"G1 X91.253 Y39.958 E0.81619\n"
"G1 X91.415 Y39.773 E0.82875\n"
"G1 X91.717 Y39.559 E0.84765\n"
"G1 X91.945 Y39.479 E0.86001\n"
"G1 X92.165 Y39.455 E0.87132\n"
"G1 X92.521 Y39.502 E0.88966\n"
"G1 X92.723 Y39.587 E0.90091\n"
"G1 X92.948 Y39.750 E0.91511\n"
"G1 X93.135 Y39.965 E0.92962\n"
"G1 X93.270 Y40.203 E0.94363\n"
"G1 X93.366 Y40.491 E0.95915\n"
"G1 X93.425 Y40.875 E0.97900\n"
"G1 X93.432 Y41.269 E0.99911\n"
"G1 X93.400 Y41.629 E1.01763\n"
"G1 X93.336 Y41.918 E1.03274\n"
"G1 X93.205 Y42.248 E1.05090\n"
"G1 X93.061 Y42.478 E1.06477\n"
"G1 X92.849 Y42.707 E1.08069\n"
"G1 X92.623 Y42.873 E1.09504\n"
"G1 X92.373 Y42.972 E1.10879\n"
"G1 X92.089 Y43.003 E1.12338\n"
"G1 X91.790 Y42.954 E1.13884\n"
"G1 X91.604 Y42.874 E1.14920\n"
"G1 X91.385 Y42.714 E1.16307\n"
"G1 X91.205 Y42.499 E1.17741\n"
"G1 X91.070 Y42.220 E1.19321\n"
"G1 X90.989 Y41.922 E1.20899\n"
"G1 X90.941 Y41.536 E1.22888\n"
"G1 X90.935 Y41.195 E1.24632\n"
"G1 E0.49632 F3000.00000\n"
"G92 E0\n"
"G1 Z0.550 F7800.000\n"
"G1 X93.238 Y39.515 F7800.000\n"
"G1 Z0.250 F7800.000\n"
"G1 E0.75000 F3000.00000\n"
"G1 X93.458 Y39.160 E0.77134 F1500.000\n"
"G1 X93.541 Y38.858 E0.78735\n"
"G1 X93.437 Y37.940 E0.83456\n"
"G1 X93.364 Y37.550 E0.85483\n"
"G1 X93.282 Y37.264 E0.87004\n"
"G1 X93.165 Y36.988 E0.88533\n"
"G1 X92.979 Y36.715 E0.90223\n"
"G1 X92.803 Y36.542 E0.91484\n"
"G1 X92.657 Y36.442 E0.92390\n"
"G1 X92.421 Y36.343 E0.93700\n"
"G1 X92.155 Y36.301 E0.95076\n"
"G1 X91.873 Y36.319 E0.96518\n"
"G1 X91.617 Y36.418 E0.97923\n"
"G1 X91.469 Y36.526 E0.98860\n"
"G1 X91.331 Y36.672 E0.99889\n"
"G1 X91.209 Y36.890 E1.01161\n"
"G1 X91.056 Y37.278 E1.03294\n"
"G1 X90.891 Y37.284 E1.04138\n"
"G1 X90.280 Y37.182 E1.07304\n"
"G1 X90.123 Y37.121 E1.08162\n"
"G1 X90.124 Y36.930 E1.09139\n"
"G1 X90.156 Y36.799 E1.09832\n"
"G1 X90.296 Y36.397 E1.12006\n"
"G1 X90.391 Y36.203 E1.13107\n"
"G1 X90.535 Y35.976 E1.14486\n"
"G1 X90.693 Y35.789 E1.15734\n"
"G1 X91.055 Y35.515 E1.18052\n"
"G1 X91.296 Y35.404 E1.19411\n"
"G1 X91.629 Y35.320 E1.21168\n"
"G1 X92.006 Y35.293 E1.23096\n"
"G1 X92.295 Y35.312 E1.24576\n"
"G1 X92.587 Y35.373 E1.26101\n"
"G1 X92.811 Y35.450 E1.27310\n"
"G1 X93.079 Y35.584 E1.28842\n"
"G1 X93.299 Y35.735 E1.30207\n"
"G1 X93.558 Y35.973 E1.32005\n"
"G1 X93.773 Y36.224 E1.33692\n"
"G1 X93.932 Y36.462 E1.35158\n"
"G1 X94.158 Y36.930 E1.37811\n"
"G1 X94.384 Y37.719 E1.42005\n"
"G1 X94.511 Y38.635 E1.46733\n"
"G1 X94.551 Y39.575 E1.51540\n"
"G1 X94.538 Y40.336 E1.55431\n"
"G1 X94.429 Y41.444 E1.61121\n"
"G1 X94.304 Y42.024 E1.64153\n"
"G1 X94.199 Y42.358 E1.65942\n"
"G1 X94.062 Y42.685 E1.67754\n"
"G1 X93.921 Y42.942 E1.69252\n"
"G1 X93.695 Y43.250 E1.71206\n"
"G1 X93.442 Y43.511 E1.73064\n"
"G1 X93.149 Y43.730 E1.74931\n"
"G1 X92.894 Y43.862 E1.76402\n"
"G1 X92.705 Y43.929 E1.77425\n"
"G1 X92.454 Y43.984 E1.78737\n"
"G1 X92.138 Y44.008 E1.80357\n"
"G1 X91.866 Y43.992 E1.81749\n"
"G1 X91.647 Y43.951 E1.82886\n"
"G1 X91.433 Y43.880 E1.84039\n"
"G1 X91.227 Y43.794 E1.85183\n"
"G1 X90.872 Y43.557 E1.87362\n"
"G1 X90.495 Y43.163 E1.90151\n"
"G1 X90.238 Y42.740 E1.92682\n"
"G1 X90.069 Y42.290 E1.95136\n"
"G1 X89.965 Y41.741 E1.97991\n"
"G1 X89.931 Y41.167 E2.00930\n"
"G1 X89.955 Y40.704 E2.03301\n"
"G1 X90.026 Y40.280 E2.05498\n"
"G1 X90.106 Y39.999 E2.06992\n"
"G1 X90.240 Y39.671 E2.08804\n"
"G1 X90.404 Y39.386 E2.10482\n"
"G1 X90.567 Y39.167 E2.11877\n"
"G1 X90.919 Y38.827 E2.14378\n"
"G1 X91.085 Y38.710 E2.15417\n"
"G1 X91.325 Y38.583 E2.16807\n"
"G1 X91.638 Y38.482 E2.18487\n"
"G1 X91.964 Y38.448 E2.20159\n"
"G1 X92.238 Y38.475 E2.21566\n"
"G1 X92.421 Y38.524 E2.22537\n"
"G1 X92.640 Y38.620 E2.23756\n"
"G1 X92.916 Y38.816 E2.25486\n"
"G1 X93.131 Y39.045 E2.27091\n"
"G1 X93.428 Y39.209 E2.28826\n"
"G1 X93.714 Y39.956 E2.32914\n"
"G1 E1.57914 F3000.00000\n"
"G92 E0\n"
"G1 Z0.550 F7800.000\n"
"G1 X100.756 Y39.658 F7800.000\n"
"G1 Z0.250 F7800.000\n"
"G1 E0.75000 F3000.00000\n"
"G1 X130.715 Y39.671 E2.28114 F1500.000\n"
"G1 X130.784 Y39.690 E2.28481\n"
"G1 X130.863 Y39.745 E2.28972\n"
"G1 X130.937 Y39.861 E2.29674\n"
"G1 X130.954 Y40.375 E2.32305\n"
"G1 X130.884 Y40.550 E2.33267\n"
"G1 X130.827 Y40.603 E2.33665\n"
"G1 X130.677 Y40.657 E2.34478\n"
"G1 X101.269 Y40.658 E3.84778\n"
"G1 X101.179 Y40.672 E3.85243\n"
"G1 X101.110 Y40.698 E3.85618\n"
"G1 X101.013 Y40.788 E3.86299\n"
"G1 X100.989 Y40.824 E3.86519\n"
"G1 X100.960 Y40.981 E3.87332\n"
"G1 X100.957 Y46.751 E4.16821\n"
"G1 X100.988 Y46.859 E4.17394\n"
"G1 X101.048 Y46.948 E4.17943\n"
"G1 X101.139 Y47.004 E4.18491\n"
"G1 X101.238 Y47.029 E4.19015\n"
"G1 X130.636 Y47.033 E5.69261\n"
"G1 X130.764 Y47.060 E5.69930\n"
"G1 X130.865 Y47.122 E5.70534\n"
"G1 X130.934 Y47.237 E5.71219\n"
"G1 X130.956 Y47.351 E5.71810\n"
"G1 X130.954 Y47.746 E5.73830\n"
"G1 X130.907 Y47.889 E5.74600\n"
"G1 X130.805 Y47.992 E5.75341\n"
"G1 X130.643 Y48.033 E5.76197\n"
"G1 X101.269 Y48.033 E7.26320\n"
"G1 X101.117 Y48.075 E7.27124\n"
"G1 X101.009 Y48.166 E7.27845\n"
"G1 X100.968 Y48.261 E7.28374\n"
"G1 X100.956 Y48.347 E7.28816\n"
"G1 X100.956 Y54.095 E7.58196\n"
"G1 X100.968 Y54.181 E7.58638\n"
"G1 X101.012 Y54.279 E7.59189\n"
"G1 X101.086 Y54.351 E7.59716\n"
"G1 X101.183 Y54.398 E7.60266\n"
"G1 X130.634 Y54.408 E9.10784\n"
"G1 X130.784 Y54.443 E9.11574\n"
"G1 X130.863 Y54.495 E9.12055\n"
"G1 X130.934 Y54.612 E9.12755\n"
"G1 X130.956 Y54.726 E9.13346\n"
"G1 X130.954 Y55.121 E9.15367\n"
"G1 X130.907 Y55.264 E9.16136\n"
"G1 X130.805 Y55.367 E9.16877\n"
"G1 X130.643 Y55.408 E9.17733\n"
"G1 X101.278 Y55.408 E10.67811\n"
"G1 X101.127 Y55.443 E10.68600\n"
"G1 X101.045 Y55.497 E10.69104\n"
"G1 X100.991 Y55.580 E10.69608\n"
"G1 X100.956 Y55.730 E10.70397\n"
"G1 X100.956 Y61.470 E10.99733\n"
"G1 X100.968 Y61.556 E11.00175\n"
"G1 X101.012 Y61.654 E11.00725\n"
"G1 X101.086 Y61.726 E11.01253\n"
"G1 X101.183 Y61.773 E11.01803\n"
"G1 X130.634 Y61.783 E12.52321\n"
"G1 X130.784 Y61.818 E12.53110\n"
"G1 X130.867 Y61.872 E12.53614\n"
"G1 X130.921 Y61.955 E12.54118\n"
"G1 X130.956 Y62.105 E12.54905\n"
"G1 X130.954 Y62.496 E12.56903\n"
"G1 X130.907 Y62.639 E12.57673\n"
"G1 X130.805 Y62.742 E12.58414\n"
"G1 X130.643 Y62.783 E12.59270\n"
"G1 X101.278 Y62.788 E14.09345\n"
"G1 X101.122 Y62.817 E14.10158\n"
"G1 X101.011 Y62.912 E14.10905\n"
"G1 X100.957 Y63.062 E14.11719\n"
"G1 X100.956 Y68.845 E14.41277\n"
"G1 X100.968 Y68.931 E14.41719\n"
"G1 X101.012 Y69.029 E14.42269\n"
"G1 X101.086 Y69.101 E14.42797\n"
"G1 X101.183 Y69.148 E14.43347\n"
"G1 X130.643 Y69.158 E15.93911\n"
"G1 X130.805 Y69.200 E15.94767\n"
"G1 X130.907 Y69.303 E15.95508\n"
"G1 X130.954 Y69.446 E15.96277\n"
"G1 X130.956 Y69.839 E15.98288\n"
"G1 X130.929 Y69.967 E15.98954\n"
"G1 X130.865 Y70.071 E15.99581\n"
"G1 X130.784 Y70.123 E16.00070\n"
"G1 X130.634 Y70.158 E16.00859\n"
"G1 X101.183 Y70.169 E17.51378\n"
"G1 X101.086 Y70.216 E17.51927\n"
"G1 X101.012 Y70.287 E17.52455\n"
"G1 X100.968 Y70.386 E17.53005\n"
"G1 X100.956 Y70.472 E17.53447\n"
"G1 X100.956 Y76.220 E17.82828\n"
"G1 X100.997 Y76.373 E17.83635\n"
"G1 X101.081 Y76.476 E17.84313\n"
"G1 X101.183 Y76.521 E17.84886\n"
"G1 X101.269 Y76.533 E17.85328\n"
"G1 X130.643 Y76.533 E19.35451\n"
"G1 X130.805 Y76.575 E19.36306\n"
"G1 X130.907 Y76.678 E19.37048\n"
"G1 X130.954 Y76.821 E19.37817\n"
"G1 X130.956 Y77.214 E19.39828\n"
"G1 X130.929 Y77.342 E19.40494\n"
"G1 X130.865 Y77.446 E19.41121\n"
"G1 X130.784 Y77.498 E19.41610\n"
"G1 X130.634 Y77.533 E19.42399\n"
"G1 X101.183 Y77.544 E20.92917\n"
"G1 X101.086 Y77.591 E20.93467\n"
"G1 X101.012 Y77.662 E20.93995\n"
"G1 X100.968 Y77.761 E20.94545\n"
"G1 X100.956 Y77.847 E20.94987\n"
"G1 X100.956 Y83.591 E21.24345\n"
"G1 X100.978 Y83.709 E21.24962\n"
"G1 X101.025 Y83.797 E21.25467\n"
"G1 X101.132 Y83.874 E21.26143\n"
"G1 X101.278 Y83.908 E21.26908\n"
"G1 X130.643 Y83.908 E22.76986\n"
"G1 X130.805 Y83.950 E22.77842\n"
"G1 X130.907 Y84.053 E22.78583\n"
"G1 X130.954 Y84.196 E22.79352\n"
"G1 X130.956 Y84.587 E22.81350\n"
"G1 X130.921 Y84.737 E22.82137\n"
"G1 X130.867 Y84.819 E22.82641\n"
"G1 X130.784 Y84.873 E22.83145\n"
"G1 X130.634 Y84.908 E22.83934\n"
"G1 X101.204 Y84.914 E24.34344\n"
"G1 X101.048 Y84.994 E24.35240\n"
"G1 X100.988 Y85.083 E24.35790\n"
"G1 X100.957 Y85.191 E24.36362\n"
"G1 X100.956 Y90.970 E24.65899\n"
"G1 X100.977 Y91.085 E24.66494\n"
"G1 X101.028 Y91.174 E24.67021\n"
"G1 X101.106 Y91.242 E24.67550\n"
"G1 X101.269 Y91.283 E24.68408\n"
"G1 X130.643 Y91.283 E26.18530\n"
"G1 X130.805 Y91.325 E26.19386\n"
"G1 X130.907 Y91.428 E26.20127\n"
"G1 X130.954 Y91.571 E26.20897\n"
"G1 X130.956 Y91.964 E26.22908\n"
"G1 X130.929 Y92.092 E26.23574\n"
"G1 X130.865 Y92.196 E26.24201\n"
"G1 X130.784 Y92.248 E26.24690\n"
"G1 X130.634 Y92.283 E26.25479\n"
"G1 X101.204 Y92.289 E27.75889\n"
"G1 X101.048 Y92.369 E27.76785\n"
"G1 X100.988 Y92.458 E27.77334\n"
"G1 X100.957 Y92.566 E27.77907\n"
"G1 X100.956 Y98.337 E28.07398\n"
"G1 X101.001 Y98.505 E28.08291\n"
"G1 X101.045 Y98.569 E28.08688\n"
"G1 X101.109 Y98.613 E28.09085\n"
"G1 X101.278 Y98.658 E28.09978\n"
"G1 X130.638 Y98.658 E29.60034\n"
"G1 X130.827 Y98.714 E29.61040\n"
"G1 X130.881 Y98.764 E29.61415\n"
"G1 X130.937 Y98.861 E29.61988\n"
"G1 X130.954 Y98.937 E29.62389\n"
"G1 X130.956 Y99.337 E29.64432\n"
"G1 X130.912 Y99.506 E29.65324\n"
"G1 X130.830 Y99.600 E29.65960\n"
"G1 X130.728 Y99.646 E29.66532\n"
"G1 X130.643 Y99.658 E29.66974\n"
"G1 X100.756 Y99.658 E31.19720\n"
"G1 E30.44720 F3000.00000\n"
"G92 E0\n"
"G1 Z0.550 F7800.000\n"
"G1 X161.334 Y190.781 F7800.000\n"
"G1 Z0.250 F7800.000\n"
"G1 E0.75000 F3000.00000\n"
"G1 X161.554 Y190.427 E0.77134 F1500.000\n"
"G1 X161.637 Y190.124 E0.78735\n"
"G1 X161.533 Y189.207 E0.83456\n"
"G1 X161.460 Y188.817 E0.85483\n"
"G1 X161.378 Y188.531 E0.87004\n"
"G1 X161.261 Y188.255 E0.88534\n"
"G1 X161.075 Y187.982 E0.90223\n"
"G1 X160.899 Y187.809 E0.91484\n"
"G1 X160.753 Y187.709 E0.92390\n"
"G1 X160.517 Y187.610 E0.93700\n"
"G1 X160.251 Y187.567 E0.95076\n"
"G1 X159.970 Y187.588 E0.96517\n"
"G1 X159.837 Y187.624 E0.97221\n"
"G1 X159.714 Y187.686 E0.97926\n"
"G1 X159.565 Y187.793 E0.98861\n"
"G1 X159.427 Y187.939 E0.99890\n"
"G1 X159.305 Y188.157 E1.01162\n"
"G1 X159.152 Y188.545 E1.03295\n"
"G1 X158.987 Y188.551 E1.04139\n"
"G1 X158.376 Y188.449 E1.07306\n"
"G1 X158.220 Y188.388 E1.08163\n"
"G1 X158.220 Y188.197 E1.09140\n"
"G1 X158.252 Y188.065 E1.09833\n"
"G1 X158.392 Y187.664 E1.12007\n"
"G1 X158.487 Y187.470 E1.13109\n"
"G1 X158.631 Y187.243 E1.14487\n"
"G1 X158.790 Y187.056 E1.15735\n"
"G1 X159.151 Y186.782 E1.18053\n"
"G1 X159.392 Y186.671 E1.19412\n"
"G1 X159.725 Y186.587 E1.21169\n"
"G1 X160.102 Y186.560 E1.23097\n"
"G1 X160.391 Y186.579 E1.24577\n"
"G1 X160.683 Y186.640 E1.26102\n"
"G1 X160.907 Y186.716 E1.27311\n"
"G1 X161.175 Y186.851 E1.28843\n"
"G1 X161.395 Y187.002 E1.30208\n"
"G1 X161.654 Y187.240 E1.32007\n"
"G1 X161.869 Y187.491 E1.33693\n"
"G1 X162.028 Y187.728 E1.35155\n"
"G1 X162.254 Y188.197 E1.37812\n"
"G1 X162.480 Y188.986 E1.42007\n"
"G1 X162.607 Y189.902 E1.46734\n"
"G1 X162.647 Y190.842 E1.51542\n"
"G1 X162.634 Y191.603 E1.55432\n"
"G1 X162.526 Y192.711 E1.61123\n"
"G1 X162.401 Y193.291 E1.64155\n"
"G1 X162.295 Y193.625 E1.65944\n"
"G1 X162.158 Y193.952 E1.67756\n"
"G1 X162.017 Y194.208 E1.69253\n"
"G1 X161.791 Y194.517 E1.71208\n"
"G1 X161.538 Y194.778 E1.73066\n"
"G1 X161.245 Y194.996 E1.74932\n"
"G1 X160.990 Y195.129 E1.76404\n"
"G1 X160.801 Y195.196 E1.77426\n"
"G1 X160.550 Y195.250 E1.78738\n"
"G1 X160.234 Y195.275 E1.80358\n"
"G1 X159.962 Y195.258 E1.81750\n"
"G1 X159.744 Y195.218 E1.82887\n"
"G1 X159.530 Y195.147 E1.84040\n"
"G1 X159.323 Y195.061 E1.85185\n"
"G1 X158.968 Y194.824 E1.87364\n"
"G1 X158.591 Y194.430 E1.90152\n"
"G1 X158.335 Y194.008 E1.92674\n"
"G1 X158.165 Y193.557 E1.95138\n"
"G1 X158.061 Y193.008 E1.97992\n"
"G1 X158.027 Y192.434 E2.00931\n"
"G1 X158.051 Y191.971 E2.03302\n"
"G1 X158.122 Y191.547 E2.05500\n"
"G1 X158.202 Y191.266 E2.06994\n"
"G1 X158.336 Y190.938 E2.08806\n"
"G1 X158.500 Y190.653 E2.10483\n"
"G1 X158.663 Y190.434 E2.11879\n"
"G1 X159.015 Y190.094 E2.14380\n"
"G1 X159.181 Y189.977 E2.15418\n"
"G1 X159.422 Y189.850 E2.16809\n"
"G1 X159.734 Y189.749 E2.18488\n"
"G1 X160.060 Y189.715 E2.20161\n"
"G1 X160.334 Y189.742 E2.21568\n"
"G1 X160.518 Y189.791 E2.22539\n"
"G1 X160.736 Y189.887 E2.23758\n"
"G1 X161.012 Y190.083 E2.25488\n"
"G1 X161.227 Y190.312 E2.27093\n"
"G1 X161.524 Y190.476 E2.28827\n"
"G1 X161.810 Y191.223 E2.32916\n"
"G1 E1.57916 F3000.00000\n"
"G92 E0\n"
"G1 Z0.550 F7800.000\n"
"G1 X159.031 Y192.462 F7800.000\n"
"G1 Z0.250 F7800.000\n"
"G1 E0.75000 F3000.00000\n"
"G1 X159.055 Y192.082 E0.76943 F1500.000\n"
"G1 X159.106 Y191.790 E0.78460\n"
"G1 X159.242 Y191.406 E0.80541\n"
"G1 X159.349 Y191.225 E0.81619\n"
"G1 X159.511 Y191.040 E0.82875\n"
"G1 X159.813 Y190.826 E0.84765\n"
"G1 X160.041 Y190.746 E0.86001\n"
"G1 X160.321 Y190.720 E0.87436\n"
"G1 X160.587 Y190.759 E0.88811\n"
"G1 X160.814 Y190.852 E0.90066\n"
"G1 X161.109 Y191.085 E0.91987\n"
"G1 X161.261 Y191.277 E0.93238\n"
"G1 X161.366 Y191.470 E0.94362\n"
"G1 X161.462 Y191.758 E0.95914\n"
"G1 X161.521 Y192.142 E0.97899\n"
"G1 X161.528 Y192.535 E0.99910\n"
"G1 X161.496 Y192.896 E1.01762\n"
"G1 X161.432 Y193.185 E1.03273\n"
"G1 X161.301 Y193.515 E1.05089\n"
"G1 X161.157 Y193.745 E1.06476\n"
"G1 X160.945 Y193.974 E1.08068\n"
"G1 X160.719 Y194.140 E1.09503\n"
"G1 X160.469 Y194.239 E1.10878\n"
"G1 X160.185 Y194.270 E1.12337\n"
"G1 X159.886 Y194.221 E1.13883\n"
"G1 X159.700 Y194.141 E1.14919\n"
"G1 X159.481 Y193.980 E1.16306\n"
"G1 X159.301 Y193.765 E1.17740\n"
"G1 X159.166 Y193.487 E1.19320\n"
"G1 X159.085 Y193.189 E1.20898\n"
"G1 X159.038 Y192.810 E1.22851\n"
"G1 X159.031 Y192.462 E1.24631\n"
"G1 E0.49631 F3000.00000\n"
"G92 E0\n"
"G1 Z0.550 F7800.000\n"
"G1 X160.956 Y180.358 F7800.000\n"
"G1 Z0.250 F7800.000\n"
"G1 E0.75000 F3000.00000\n"
"G1 X160.956 Y150.472 E2.27745 F1500.000\n"
"G1 X160.913 Y150.310 E2.28599\n"
"G1 X160.870 Y150.250 E2.28974\n"
"G1 X160.803 Y150.204 E2.29393\n"
"G1 X160.634 Y150.159 E2.30285\n"
"G1 X160.235 Y150.160 E2.32328\n"
"G1 X160.154 Y150.178 E2.32751\n"
"G1 X160.012 Y150.287 E2.33667\n"
"G1 X159.968 Y150.386 E2.34217\n"
"G1 X159.956 Y150.472 E2.34659\n"
"G1 X159.956 Y179.841 E3.84760\n"
"G1 X159.934 Y179.960 E3.85377\n"
"G1 X159.862 Y180.071 E3.86054\n"
"G1 X159.786 Y180.127 E3.86537\n"
"G1 X159.633 Y180.154 E3.87328\n"
"G1 X153.864 Y180.157 E4.16816\n"
"G1 X153.756 Y180.127 E4.17389\n"
"G1 X153.667 Y180.066 E4.17939\n"
"G1 X153.587 Y179.910 E4.18835\n"
"G1 X153.581 Y150.479 E5.69252\n"
"G1 X153.551 Y150.342 E5.69968\n"
"G1 X153.493 Y150.250 E5.70525\n"
"G1 X153.377 Y150.180 E5.71215\n"
"G1 X153.264 Y150.159 E5.71806\n"
"G1 X152.868 Y150.160 E5.73826\n"
"G1 X152.725 Y150.207 E5.74596\n"
"G1 X152.623 Y150.309 E5.75337\n"
"G1 X152.581 Y150.472 E5.76193\n"
"G1 X152.581 Y179.845 E7.26316\n"
"G1 X152.567 Y179.935 E7.26780\n"
"G1 X152.492 Y180.069 E7.27567\n"
"G1 X152.398 Y180.131 E7.28138\n"
"G1 X152.268 Y180.158 E7.28820\n"
"G1 X146.489 Y180.157 E7.58356\n"
"G1 X146.381 Y180.127 E7.58928\n"
"G1 X146.292 Y180.066 E7.59478\n"
"G1 X146.212 Y179.910 E7.60374\n"
"G1 X146.206 Y150.479 E9.10791\n"
"G1 X146.176 Y150.342 E9.11507\n"
"G1 X146.118 Y150.250 E9.12064\n"
"G1 X146.002 Y150.180 E9.12754\n"
"G1 X145.889 Y150.159 E9.13345\n"
"G1 X145.493 Y150.160 E9.15366\n"
"G1 X145.350 Y150.207 E9.16136\n"
"G1 X145.248 Y150.309 E9.16876\n"
"G1 X145.206 Y150.472 E9.17732\n"
"G1 X145.206 Y179.837 E10.67810\n"
"G1 X145.172 Y179.983 E10.68576\n"
"G1 X145.094 Y180.089 E10.69253\n"
"G1 X145.007 Y180.135 E10.69757\n"
"G1 X144.888 Y180.158 E10.70373\n"
"G1 X139.148 Y180.158 E10.99709\n"
"G1 X139.027 Y180.135 E11.00338\n"
"G1 X138.952 Y180.098 E11.00767\n"
"G1 X138.889 Y180.029 E11.01243\n"
"G1 X138.841 Y179.931 E11.01800\n"
"G1 X138.831 Y150.479 E12.52326\n"
"G1 X138.801 Y150.342 E12.53042\n"
"G1 X138.743 Y150.250 E12.53599\n"
"G1 X138.627 Y150.180 E12.54289\n"
"G1 X138.514 Y150.159 E12.54880\n"
"G1 X138.118 Y150.160 E12.56900\n"
"G1 X137.975 Y150.207 E12.57670\n"
"G1 X137.873 Y150.309 E12.58411\n"
"G1 X137.831 Y150.472 E12.59267\n"
"G1 X137.816 Y179.944 E14.09894\n"
"G1 X137.722 Y180.087 E14.10767\n"
"G1 X137.665 Y180.125 E14.11120\n"
"G1 X137.509 Y180.158 E14.11935\n"
"G1 X131.773 Y180.158 E14.41249\n"
"G1 X131.623 Y180.122 E14.42037\n"
"G1 X131.514 Y180.030 E14.42770\n"
"G1 X131.466 Y179.932 E14.43328\n"
"G1 X131.457 Y179.845 E14.43771\n"
"G1 X131.456 Y150.472 E15.93894\n"
"G1 X131.413 Y150.310 E15.94747\n"
"G1 X131.312 Y150.206 E15.95488\n"
"G1 X131.168 Y150.160 E15.96259\n"
"G1 X130.773 Y150.159 E15.98280\n"
"G1 X130.658 Y150.178 E15.98873\n"
"G1 X130.542 Y150.251 E15.99575\n"
"G1 X130.490 Y150.329 E16.00057\n"
"G1 X130.456 Y150.480 E16.00848\n"
"G1 X130.445 Y179.931 E17.51366\n"
"G1 X130.351 Y180.085 E17.52285\n"
"G1 X130.257 Y180.136 E17.52834\n"
"G1 X130.138 Y180.158 E17.53451\n"
"G1 X124.381 Y180.157 E17.82876\n"
"G1 X124.287 Y180.138 E17.83364\n"
"G1 X124.205 Y180.095 E17.83836\n"
"G1 X124.115 Y179.992 E17.84538\n"
"G1 X124.081 Y179.837 E17.85353\n"
"G1 X124.081 Y150.472 E19.35431\n"
"G1 X124.039 Y150.309 E19.36286\n"
"G1 X123.936 Y150.207 E19.37027\n"
"G1 X123.793 Y150.160 E19.37797\n"
"G1 X123.402 Y150.159 E19.39795\n"
"G1 X123.252 Y150.193 E19.40582\n"
"G1 X123.170 Y150.247 E19.41086\n"
"G1 X123.116 Y150.330 E19.41590\n"
"G1 X123.081 Y150.480 E19.42379\n"
"G1 X123.070 Y179.931 E20.92898\n"
"G1 X122.976 Y180.085 E20.93816\n"
"G1 X122.882 Y180.136 E20.94366\n"
"G1 X122.763 Y180.158 E20.94982\n"
"G1 X117.025 Y180.158 E21.24308\n"
"G1 X116.897 Y180.132 E21.24978\n"
"G1 X116.795 Y180.068 E21.25595\n"
"G1 X116.737 Y179.974 E21.26158\n"
"G1 X116.706 Y179.838 E21.26873\n"
"G1 X116.706 Y150.472 E22.76959\n"
"G1 X116.664 Y150.309 E22.77815\n"
"G1 X116.561 Y150.207 E22.78556\n"
"G1 X116.418 Y150.160 E22.79325\n"
"G1 X116.023 Y150.159 E22.81346\n"
"G1 X115.909 Y150.180 E22.81937\n"
"G1 X115.792 Y150.251 E22.82637\n"
"G1 X115.741 Y150.330 E22.83118\n"
"G1 X115.706 Y150.480 E22.83907\n"
"G1 X115.705 Y179.845 E24.33985\n"
"G1 X115.695 Y179.931 E24.34428\n"
"G1 X115.601 Y180.085 E24.35347\n"
"G1 X115.507 Y180.136 E24.35896\n"
"G1 X115.388 Y180.158 E24.36513\n"
"G1 X109.644 Y180.158 E24.65871\n"
"G1 X109.558 Y180.147 E24.66314\n"
"G1 X109.464 Y180.105 E24.66842\n"
"G1 X109.368 Y180.000 E24.67567\n"
"G1 X109.343 Y179.935 E24.67920\n"
"G1 X109.331 Y179.845 E24.68386\n"
"G1 X109.331 Y150.472 E26.18508\n"
"G1 X109.296 Y150.326 E26.19275\n"
"G1 X109.242 Y150.247 E26.19761\n"
"G1 X109.147 Y150.190 E26.20331\n"
"G1 X109.043 Y150.160 E26.20881\n"
"G1 X108.652 Y150.159 E26.22879\n"
"G1 X108.502 Y150.193 E26.23666\n"
"G1 X108.420 Y150.247 E26.24170\n"
"G1 X108.376 Y150.311 E26.24566\n"
"G1 X108.331 Y150.480 E26.25460\n"
"G1 X108.325 Y179.910 E27.75869\n"
"G1 X108.236 Y180.076 E27.76830\n"
"G1 X108.157 Y180.126 E27.77306\n"
"G1 X108.031 Y180.157 E27.77972\n"
"G1 X102.278 Y180.158 E28.07375\n"
"G1 X102.108 Y180.114 E28.08269\n"
"G1 X102.064 Y180.087 E28.08536\n"
"G1 X102.001 Y180.005 E28.09064\n"
"G1 X101.956 Y179.845 E28.09914\n"
"G1 X101.956 Y150.476 E29.60014\n"
"G1 X101.935 Y150.362 E29.60608\n"
"G1 X101.863 Y150.245 E29.61308\n"
"G1 X101.783 Y150.192 E29.61797\n"
"G1 X101.670 Y150.162 E29.62398\n"
"G1 X101.210 Y150.169 E29.64747\n"
"G1 X101.099 Y150.205 E29.65343\n"
"G1 X100.997 Y150.309 E29.66089\n"
"G1 X100.956 Y150.472 E29.66946\n"
"G1 X100.956 Y180.358 E31.19691\n"
"G92 E0\n"
"G1 Z0.550 F7800.000\n"
"\n"
"\n"
"\n"
"\n"
"M104 S140\n"
"\n"
"\n"
"\n"
"\n"
"\n"
"\n"
"G1 X1.0 Y243.8 Z0.3 F7800.0\n"
"T1\n"
"M109 T1 S190\n"
"M117 Purge Extruder 2\n"
"G92 E0\n"
"\n"
"G1 X128.5 Y243.8 Z0.3 F1500.0 E15\n"
"\n"
"G1 E14 F3000\n"
"G92 E0\n"
"M104 T1 S190\n"
"\n"
"\n"
"\n"
"\n"
"M117 Purge Extruder 1\n"
"G92 E0\n"
"G1 E-0.5 F3000\n"
"\n"
"G1 X161.356 Y119.458 F7800.000\n"
"G1 E0 F3000\n"
"T1\n"
"G92 E0\n"
"G1 Z0.250 F7800.000\n"
"G1 E0.75000 F3000.00000\n"
"G1 X161.356 Y149.242 E2.27217 F1500.000\n"
"G1 X161.319 Y149.417 E2.28130\n"
"G1 X161.224 Y149.550 E2.28965\n"
"G1 X161.100 Y149.627 E2.29713\n"
"G1 X160.944 Y149.658 E2.30524\n"
"G1 X160.732 Y149.653 E2.31606\n"
"G1 X160.575 Y149.611 E2.32442\n"
"G1 X160.441 Y149.504 E2.33317\n"
"G1 X160.358 Y149.283 E2.34522\n"
"G1 X160.356 Y120.079 E3.83777\n"
"G1 X160.320 Y119.899 E3.84717\n"
"G1 X160.222 Y119.763 E3.85572\n"
"G1 X160.120 Y119.695 E3.86199\n"
"G1 X159.935 Y119.658 E3.87160\n"
"G1 X154.256 Y119.660 E4.16184\n"
"G1 X154.123 Y119.698 E4.16893\n"
"G1 X154.010 Y119.767 E4.17566\n"
"G1 X153.893 Y119.962 E4.18731\n"
"G1 X153.881 Y149.238 E5.68352\n"
"G1 X153.844 Y149.422 E5.69314\n"
"G1 X153.776 Y149.524 E5.69941\n"
"G1 X153.641 Y149.623 E5.70796\n"
"G1 X153.465 Y149.658 E5.71709\n"
"G1 X153.185 Y149.643 E5.73146\n"
"G1 X153.031 Y149.565 E5.74024\n"
"G1 X152.919 Y149.426 E5.74937\n"
"G1 X152.881 Y149.238 E5.75920\n"
"G1 X152.881 Y120.071 E7.24985\n"
"G1 X152.833 Y119.872 E7.26028\n"
"G1 X152.745 Y119.765 E7.26738\n"
"G1 X152.671 Y119.707 E7.27220\n"
"G1 X152.468 Y119.658 E7.28284\n"
"G1 X146.781 Y119.660 E7.57350\n"
"G1 X146.646 Y119.698 E7.58066\n"
"G1 X146.544 Y119.758 E7.58669\n"
"G1 X146.478 Y119.841 E7.59216\n"
"G1 X146.418 Y119.965 E7.59915\n"
"G1 X146.406 Y149.242 E9.09545\n"
"G1 X146.383 Y149.384 E9.10279\n"
"G1 X146.298 Y149.527 E9.11133\n"
"G1 X146.166 Y149.623 E9.11967\n"
"G1 X145.990 Y149.658 E9.12880\n"
"G1 X145.710 Y149.643 E9.14317\n"
"G1 X145.563 Y149.571 E9.15153\n"
"G1 X145.452 Y149.440 E9.16026\n"
"G1 X145.406 Y149.246 E9.17047\n"
"G1 X145.406 Y120.071 E10.66155\n"
"G1 X145.360 Y119.876 E10.67177\n"
"G1 X145.247 Y119.742 E10.68075\n"
"G1 X145.134 Y119.685 E10.68721\n"
"G1 X144.989 Y119.658 E10.69473\n"
"G1 X139.306 Y119.660 E10.98518\n"
"G1 X139.173 Y119.698 E10.99227\n"
"G1 X139.061 Y119.766 E10.99893\n"
"G1 X138.944 Y119.960 E11.01056\n"
"G1 X138.931 Y149.238 E12.50685\n"
"G1 X138.894 Y149.420 E12.51638\n"
"G1 X138.816 Y149.536 E12.52353\n"
"G1 X138.692 Y149.622 E12.53124\n"
"G1 X138.515 Y149.658 E12.54044\n"
"G1 X138.235 Y149.643 E12.55481\n"
"G1 X138.095 Y149.575 E12.56275\n"
"G1 X137.974 Y149.438 E12.57206\n"
"G1 X137.931 Y149.246 E12.58214\n"
"G1 X137.931 Y120.075 E14.07300\n"
"G1 X137.908 Y119.929 E14.08056\n"
"G1 X137.843 Y119.810 E14.08747\n"
"G1 X137.702 Y119.698 E14.09670\n"
"G1 X137.518 Y119.658 E14.10628\n"
"G1 X131.872 Y119.658 E14.39483\n"
"G1 X131.728 Y119.687 E14.40234\n"
"G1 X131.611 Y119.744 E14.40901\n"
"G1 X131.527 Y119.843 E14.41567\n"
"G1 X131.468 Y119.965 E14.42255\n"
"G1 X131.456 Y149.238 E15.91864\n"
"G1 X131.419 Y149.421 E15.92817\n"
"G1 X131.341 Y149.536 E15.93532\n"
"G1 X131.217 Y149.622 E15.94302\n"
"G1 X131.040 Y149.658 E15.95223\n"
"G1 X130.760 Y149.643 E15.96659\n"
"G1 X130.606 Y149.566 E15.97538\n"
"G1 X130.496 Y149.425 E15.98449\n"
"G1 X130.456 Y149.238 E15.99430\n"
"G1 X130.456 Y120.071 E17.48495\n"
"G1 X130.442 Y119.965 E17.49041\n"
"G1 X130.387 Y119.842 E17.49729\n"
"G1 X130.300 Y119.744 E17.50397\n"
"G1 X130.184 Y119.685 E17.51064\n"
"G1 X130.039 Y119.658 E17.51816\n"
"G1 X124.352 Y119.661 E17.80882\n"
"G1 X124.163 Y119.725 E17.81903\n"
"G1 X124.089 Y119.790 E17.82406\n"
"G1 X124.019 Y119.900 E17.83071\n"
"G1 X123.984 Y120.071 E17.83966\n"
"G1 X123.981 Y149.238 E19.33030\n"
"G1 X123.942 Y149.426 E19.34013\n"
"G1 X123.830 Y149.565 E19.34926\n"
"G1 X123.677 Y149.643 E19.35804\n"
"G1 X123.396 Y149.658 E19.37240\n"
"G1 X123.217 Y149.621 E19.38175\n"
"G1 X123.115 Y149.554 E19.38802\n"
"G1 X123.017 Y149.418 E19.39657\n"
"G1 X122.981 Y149.238 E19.40597\n"
"G1 X122.969 Y119.965 E20.90205\n"
"G1 X122.910 Y119.843 E20.90893\n"
"G1 X122.826 Y119.744 E20.91560\n"
"G1 X122.708 Y119.687 E20.92226\n"
"G1 X122.564 Y119.658 E20.92977\n"
"G1 X116.927 Y119.658 E21.21790\n"
"G1 X116.746 Y119.694 E21.22731\n"
"G1 X116.614 Y119.789 E21.23565\n"
"G1 X116.533 Y119.935 E21.24415\n"
"G1 X116.506 Y120.075 E21.25144\n"
"G1 X116.506 Y149.238 E22.74188\n"
"G1 X116.466 Y149.426 E22.75170\n"
"G1 X116.356 Y149.566 E22.76082\n"
"G1 X116.202 Y149.643 E22.76960\n"
"G1 X115.921 Y149.658 E22.78397\n"
"G1 X115.746 Y149.623 E22.79310\n"
"G1 X115.611 Y149.526 E22.80157\n"
"G1 X115.537 Y149.408 E22.80872\n"
"G1 X115.506 Y149.239 E22.81749\n"
"G1 X115.494 Y119.965 E24.31364\n"
"G1 X115.378 Y119.768 E24.32527\n"
"G1 X115.264 Y119.698 E24.33214\n"
"G1 X115.130 Y119.660 E24.33923\n"
"G1 X109.443 Y119.658 E24.62989\n"
"G1 X109.338 Y119.675 E24.63533\n"
"G1 X109.209 Y119.727 E24.64243\n"
"G1 X109.080 Y119.869 E24.65222\n"
"G1 X109.031 Y120.071 E24.66285\n"
"G1 X109.031 Y149.238 E26.15351\n"
"G1 X108.991 Y149.426 E26.16333\n"
"G1 X108.881 Y149.566 E26.17244\n"
"G1 X108.727 Y149.643 E26.18123\n"
"G1 X108.444 Y149.658 E26.19571\n"
"G1 X108.291 Y149.631 E26.20366\n"
"G1 X108.163 Y149.552 E26.21135\n"
"G1 X108.066 Y149.418 E26.21976\n"
"G1 X108.031 Y149.238 E26.22917\n"
"G1 X108.019 Y119.965 E27.72525\n"
"G1 X107.959 Y119.842 E27.73220\n"
"G1 X107.885 Y119.751 E27.73822\n"
"G1 X107.791 Y119.698 E27.74370\n"
"G1 X107.655 Y119.660 E27.75091\n"
"G1 X101.968 Y119.658 E28.04157\n"
"G1 X101.766 Y119.707 E28.05222\n"
"G1 X101.622 Y119.840 E28.06223\n"
"G1 X101.583 Y119.926 E28.06704\n"
"G1 X101.556 Y120.075 E28.07478\n"
"G1 X101.556 Y149.246 E29.56565\n"
"G1 X101.510 Y149.441 E29.57588\n"
"G1 X101.399 Y149.570 E29.58462\n"
"G1 X101.307 Y149.621 E29.58999\n"
"G1 X101.229 Y149.647 E29.59419\n"
"G1 X100.971 Y149.658 E29.60735\n"
"G1 X100.796 Y149.623 E29.61648\n"
"G1 X100.660 Y149.525 E29.62504\n"
"G1 X100.607 Y149.447 E29.62986\n"
"G1 X100.556 Y149.238 E29.64087\n"
"G1 X100.556 Y119.458 E31.16282\n"
"G1 E30.41282 F3000.00000\n"
"G92 E0\n"
"G1 Z0.550 F7800.000\n"
"G1 X161.656 Y100.058 F7800.000\n"
"G1 Z0.250 F7800.000\n"
"G1 E0.75000 F3000.00000\n"
"G1 X131.868 Y100.058 E2.27238 F1500.000\n"
"G1 X131.671 Y100.009 E2.28277\n"
"G1 X131.544 Y99.900 E2.29129\n"
"G1 X131.478 Y99.779 E2.29835\n"
"G1 X131.458 Y99.689 E2.30307\n"
"G1 X131.463 Y99.395 E2.31812\n"
"G1 X131.495 Y99.300 E2.32321\n"
"G1 X131.586 Y99.166 E2.33152\n"
"G1 X131.698 Y99.098 E2.33819\n"
"G1 X131.831 Y99.060 E2.34528\n"
"G1 X161.039 Y99.058 E3.83804\n"
"G1 X161.200 Y99.027 E3.84640\n"
"G1 X161.302 Y98.968 E3.85244\n"
"G1 X161.374 Y98.896 E3.85765\n"
"G1 X161.426 Y98.803 E3.86309\n"
"G1 X161.456 Y98.646 E3.87125\n"
"G1 X161.456 Y93.000 E4.15980\n"
"G1 X161.428 Y92.856 E4.16731\n"
"G1 X161.370 Y92.739 E4.17398\n"
"G1 X161.271 Y92.654 E4.18064\n"
"G1 X161.150 Y92.596 E4.18752\n"
"G1 X131.898 Y92.584 E5.68254\n"
"G1 X131.720 Y92.554 E5.69172\n"
"G1 X131.589 Y92.479 E5.69944\n"
"G1 X131.491 Y92.343 E5.70799\n"
"G1 X131.456 Y92.168 E5.71712\n"
"G1 X131.472 Y91.887 E5.73149\n"
"G1 X131.544 Y91.741 E5.73985\n"
"G1 X131.674 Y91.630 E5.74858\n"
"G1 X131.868 Y91.583 E5.75879\n"
"G1 X161.043 Y91.583 E7.24987\n"
"G1 X161.149 Y91.568 E7.25532\n"
"G1 X161.273 Y91.517 E7.26221\n"
"G1 X161.370 Y91.428 E7.26890\n"
"G1 X161.429 Y91.312 E7.27557\n"
"G1 X161.456 Y91.167 E7.28309\n"
"G1 X161.455 Y85.508 E7.57228\n"
"G1 X161.389 Y85.290 E7.58391\n"
"G1 X161.324 Y85.217 E7.58894\n"
"G1 X161.154 Y85.121 E7.59893\n"
"G1 X131.877 Y85.108 E9.09522\n"
"G1 X131.693 Y85.069 E9.10482\n"
"G1 X131.591 Y85.003 E9.11105\n"
"G1 X131.492 Y84.868 E9.11958\n"
"G1 X131.457 Y84.693 E9.12871\n"
"G1 X131.467 Y84.411 E9.14312\n"
"G1 X131.543 Y84.265 E9.15154\n"
"G1 X131.681 Y84.152 E9.16064\n"
"G1 X131.868 Y84.108 E9.17048\n"
"G1 X161.043 Y84.108 E10.66156\n"
"G1 X161.150 Y84.094 E10.66708\n"
"G1 X161.264 Y84.047 E10.67333\n"
"G1 X161.347 Y83.980 E10.67883\n"
"G1 X161.421 Y83.869 E10.68565\n"
"G1 X161.454 Y83.733 E10.69278\n"
"G1 X161.456 Y78.054 E10.98301\n"
"G1 X161.415 Y77.874 E10.99244\n"
"G1 X161.332 Y77.750 E11.00008\n"
"G1 X161.217 Y77.673 E11.00719\n"
"G1 X161.105 Y77.644 E11.01309\n"
"G1 X131.898 Y77.634 E12.50582\n"
"G1 X131.692 Y77.596 E12.51650\n"
"G1 X131.592 Y77.526 E12.52274\n"
"G1 X131.493 Y77.392 E12.53124\n"
"G1 X131.457 Y77.218 E12.54035\n"
"G1 X131.470 Y76.937 E12.55473\n"
"G1 X131.542 Y76.789 E12.56313\n"
"G1 X131.674 Y76.679 E12.57190\n"
"G1 X131.868 Y76.633 E12.58212\n"
"G1 X161.035 Y76.633 E14.07277\n"
"G1 X161.176 Y76.608 E14.08011\n"
"G1 X161.353 Y76.495 E14.09080\n"
"G1 X161.417 Y76.396 E14.09683\n"
"G1 X161.456 Y76.213 E14.10643\n"
"G1 X161.454 Y70.534 E14.39666\n"
"G1 X161.416 Y70.399 E14.40382\n"
"G1 X161.357 Y70.297 E14.40985\n"
"G1 X161.214 Y70.198 E14.41874\n"
"G1 X161.150 Y70.171 E14.42229\n"
"G1 X161.043 Y70.161 E14.42776\n"
"G1 X131.868 Y70.158 E15.91882\n"
"G1 X131.685 Y70.119 E15.92840\n"
"G1 X131.547 Y70.010 E15.93742\n"
"G1 X131.470 Y69.855 E15.94624\n"
"G1 X131.457 Y69.574 E15.96061\n"
"G1 X131.492 Y69.399 E15.96974\n"
"G1 X131.589 Y69.266 E15.97818\n"
"G1 X131.708 Y69.192 E15.98528\n"
"G1 X131.875 Y69.158 E15.99402\n"
"G1 X161.105 Y69.149 E17.48788\n"
"G1 X161.204 Y69.126 E17.49310\n"
"G1 X161.323 Y69.049 E17.50033\n"
"G1 X161.416 Y68.916 E17.50860\n"
"G1 X161.456 Y68.738 E17.51796\n"
"G1 X161.456 Y63.100 E17.80609\n"
"G1 X161.432 Y62.954 E17.81363\n"
"G1 X161.368 Y62.840 E17.82031\n"
"G1 X161.273 Y62.750 E17.82699\n"
"G1 X161.148 Y62.700 E17.83387\n"
"G1 X161.043 Y62.683 E17.83931\n"
"G1 X131.868 Y62.683 E19.33039\n"
"G1 X131.674 Y62.638 E19.34060\n"
"G1 X131.543 Y62.527 E19.34936\n"
"G1 X131.472 Y62.380 E19.35773\n"
"G1 X131.457 Y62.095 E19.37230\n"
"G1 X131.480 Y61.959 E19.37935\n"
"G1 X131.565 Y61.816 E19.38786\n"
"G1 X131.697 Y61.722 E19.39614\n"
"G1 X131.877 Y61.683 E19.40552\n"
"G1 X161.154 Y61.670 E20.90182\n"
"G1 X161.324 Y61.575 E20.91180\n"
"G1 X161.389 Y61.501 E20.91683\n"
"G1 X161.454 Y61.312 E20.92704\n"
"G1 X161.456 Y55.625 E21.21770\n"
"G1 X161.428 Y55.481 E21.22521\n"
"G1 X161.370 Y55.364 E21.23188\n"
"G1 X161.258 Y55.268 E21.23940\n"
"G1 X161.149 Y55.223 E21.24544\n"
"G1 X161.043 Y55.208 E21.25090\n"
"G1 X131.868 Y55.208 E22.74198\n"
"G1 X131.674 Y55.162 E22.75219\n"
"G1 X131.544 Y55.051 E22.76092\n"
"G1 X131.472 Y54.905 E22.76928\n"
"G1 X131.456 Y54.620 E22.78386\n"
"G1 X131.479 Y54.483 E22.79092\n"
"G1 X131.564 Y54.339 E22.79946\n"
"G1 X131.696 Y54.244 E22.80780\n"
"G1 X131.877 Y54.208 E22.81721\n"
"G1 X161.043 Y54.205 E24.30785\n"
"G1 X161.150 Y54.196 E24.31331\n"
"G1 X161.214 Y54.169 E24.31687\n"
"G1 X161.370 Y54.053 E24.32682\n"
"G1 X161.428 Y53.936 E24.33348\n"
"G1 X161.456 Y53.792 E24.34099\n"
"G1 X161.456 Y48.146 E24.62955\n"
"G1 X161.410 Y47.951 E24.63977\n"
"G1 X161.296 Y47.818 E24.64873\n"
"G1 X161.184 Y47.759 E24.65518\n"
"G1 X161.039 Y47.733 E24.66272\n"
"G1 X131.868 Y47.733 E26.15358\n"
"G1 X131.674 Y47.687 E26.16379\n"
"G1 X131.544 Y47.576 E26.17253\n"
"G1 X131.472 Y47.430 E26.18089\n"
"G1 X131.456 Y47.145 E26.19546\n"
"G1 X131.478 Y47.008 E26.20253\n"
"G1 X131.563 Y46.864 E26.21109\n"
"G1 X131.696 Y46.769 E26.21944\n"
"G1 X131.877 Y46.733 E26.22885\n"
"G1 X161.150 Y46.721 E27.72493\n"
"G1 X161.271 Y46.663 E27.73181\n"
"G1 X161.373 Y46.575 E27.73869\n"
"G1 X161.415 Y46.496 E27.74328\n"
"G1 X161.455 Y46.333 E27.75182\n"
"G1 X161.456 Y40.675 E28.04101\n"
"G1 X161.432 Y40.529 E28.04856\n"
"G1 X161.367 Y40.411 E28.05546\n"
"G1 X161.224 Y40.297 E28.06484\n"
"G1 X161.035 Y40.258 E28.07467\n"
"G1 X131.856 Y40.257 E29.56595\n"
"G1 X131.732 Y40.233 E29.57242\n"
"G1 X131.608 Y40.170 E29.57952\n"
"G1 X131.493 Y40.022 E29.58909\n"
"G1 X131.468 Y39.931 E29.59394\n"
"G1 X131.457 Y39.670 E29.60727\n"
"G1 X131.471 Y39.561 E29.61289\n"
"G1 X131.563 Y39.389 E29.62286\n"
"G1 X131.653 Y39.318 E29.62872\n"
"G1 X131.722 Y39.283 E29.63271\n"
"G1 X131.872 Y39.258 E29.64047\n"
"G1 X161.656 Y39.258 E31.16263\n"
"G1 E30.41263 F3000.00000\n"
"G92 E0\n"
"G1 Z0.550 F7800.000\n"
"G91\n"
"G1 Z2 F5000\n"
"G90\n"
"G1 X220 Y243 F7800\n"
"T0\n"
"M104 T0 S0\n"
"G92 E0\n"
"G1 E-10 F3000\n"
"G92 E0\n"
"T1\n"
"M104 T1 S0\n"
"G92 E0\n"
"G1 E-10 F3000\n"
"G92 E0\n"
"T0\n"
"M140 S0\n"
"M107\n"
"M84\n"
"M117 Print Complete\n"
 );
bool customMCode(GCode *com) {
  switch(com->M) {
  case 4200: // send calibration gcode
     flashSource.executeCommands(calibrationGCode,false,0);
     break;
  case 4201:
     uid.pushMenu(&ui_exy3,true);
     break;
  default:
     return false;
  }
  return true;
}
