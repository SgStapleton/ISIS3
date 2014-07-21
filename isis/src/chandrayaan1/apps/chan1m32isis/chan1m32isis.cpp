#include "Isis.h"

#include <math.h>

#include <QDebug>
#include <QString>

#include "BoxcarCachingAlgorithm.h"
#include "Brick.h"
#include "Cube.h"
#include "CubeAttribute.h"
#include "FileName.h"
#include "IException.h"
#include "ImportPdsTable.h"
#include "iTime.h"
#include "OriginalLabel.h"
#include "PixelType.h"
#include "ProcessByLine.h"
#include "ProcessBySample.h"
#include "ProcessImportPds.h"
#include "Progress.h"
#include "Pvl.h"
#include "Table.h"
#include "UserInterface.h"

using namespace std;
using namespace Isis;

void writeCube(Buffer &in);
void writeCubeWithDroppedLines(Buffer &in);

void importImage(QString outputParamName, ProcessImportPds::PdsFileType fileType);
void translateChandrayaan1M3Labels(Pvl &pdsLabel, Cube *ocube, Table &utcTable,
                                   ProcessImportPds::PdsFileType fileType);
void flip(Buffer &in);
void flipUtcTable(Table &utcTable);


Cube *g_oCube;
Brick *g_oBuff;
int g_totalLinesAdded;
double g_expectedLineRate;
Table *g_utcTable;
PvlGroup g_results("Results");

void IsisMain() {
  g_results.clear();
  importImage("TO", (ProcessImportPds::PdsFileType)(ProcessImportPds::Rdn|ProcessImportPds::L0));
  Application::Log(g_results);
  importImage("LOC", ProcessImportPds::Loc);
  importImage("OBS", ProcessImportPds::Obs);
}


void importImage(QString outputParamName, ProcessImportPds::PdsFileType fileType) {

  // We should be processing a PDS file
  UserInterface &ui = Application::GetUserInterface();
  if (!ui.WasEntered(outputParamName)) {
    return;
  }

  g_oCube = NULL;
  g_oBuff = NULL;
  g_totalLinesAdded = 0;
  g_utcTable = NULL;

  ProcessImportPds importPds;
  importPds.Progress()->SetText((QString)"Writing " + outputParamName + " file");

  FileName in = ui.GetFileName("FROM");

  Pvl pdsLabel(in.expanded());
  if (fileType == (ProcessImportPds::L0 | ProcessImportPds::Rdn)) {
    //  Is this a L0 or L1B product?
    if ((QString) pdsLabel["PRODUCT_TYPE"] == "RAW_IMAGE") {
      fileType = ProcessImportPds::L0;
    }
    else {
      fileType = ProcessImportPds::Rdn;
    }
  }

  // Convert the pds file to a cube
  try {
    importPds.SetPdsFile(in.expanded(), "", pdsLabel, fileType);
  }
  catch(IException &e) {
    QString msg = "Input file [" + in.expanded() +
                 "] does not appear to be a Chandrayaan 1 M3 detached PDS label";
    throw IException(e, IException::User, msg, _FILEINFO_);
  }

  bool samplesNeedFlipped = false;
  bool linesNeedFlipped = false;
  if (fileType != ProcessImportPds::L0) {
    // M3 PDS L1B images may be flipped/mirrored in sample and/or line to visually appear with
    // north nearly up. The ISIS camera model does not take this into account, so this post
    // acquisition processing needs to be removed. There are four possible flip/mirror mode 
    // combinations.
    // 1.  Descending yaw / Forward orbit limb - No changes in sample or line
    // 2.  Descending yaw / Reverse orbit limb - Samples are reversed, first sample on west side of image
    // 3.  Ascending yaw / Forward orbit limb - Lines/times are reversed so northernmost image line first,
    //                                          Samples are reversed, first sample on west side of image
    // 4.  Ascending yaw / Reverse orbit limb - Lines/times are reversed so northernmost image line first,
    QString yawDirection = (QString) pdsLabel["CH1:SPACECRAFT_YAW_DIRECTION"];
    QString limbDirection = (QString) pdsLabel["CH1:ORBIT_LIMB_DIRECTION"];
    samplesNeedFlipped = ( ((yawDirection == "REVERSE") && (limbDirection == "DESCENDING")) ||
                           ((yawDirection == "FORWARD") && (limbDirection == "ASCENDING")) );
    linesNeedFlipped = (limbDirection == "ASCENDING");
  }

  // The following 2 commented lines can be used for testing purposes, No flipping will be done with 
  // these lines uncommented i.e. north is always up, lons always pos east to the right.
  //    samplesNeedFlipped = false;
  //    linesNeedFlipped = false;

  {
    // Calculate the number of output lines that should be present from the start and end times 
    // in the UTC table.
    int outputLines;
    if (fileType == ProcessImportPds::Rdn || fileType == ProcessImportPds::Loc ||
        fileType == ProcessImportPds::Obs) {
      g_utcTable = &(importPds.ImportTable("UTC_FILE"));

      if (g_utcTable->Records() >= 1) {

        QString instMode = (QString) pdsLabel["INSTRUMENT_MODE_ID"];
        // Initialize to the value for a GLOBAL mode observation
        g_expectedLineRate = .10176;
        if (instMode == "TARGET") {
          g_expectedLineRate = .05088;
        }

        iTime firstEt((QString)(*g_utcTable)[0]["UtcTime"]);
        iTime lastEt((QString)(*g_utcTable)[importPds.Lines()-1]["UtcTime"]);

        outputLines = ceil(fabs(lastEt - firstEt) / g_expectedLineRate + 1.0);
      }
      else {
        QString msg = "Input file [" + in.expanded() +
                     "] does not appear to have any records in the UTC_FILE table";
        throw IException(IException::User, msg, _FILEINFO_);
      }
    }
    else {
      outputLines = importPds.Lines();
    }

    // Since the output cube possibly has more lines then the input PDS image, due to dropped 
    // lines, we have to write the output cube instead of letting ProcessImportPds do it for us.
    g_oCube = new Cube();
    if (fileType == ProcessImportPds::L0) {
      g_oCube->setPixelType(importPds.PixelType());
    }
    g_oCube->setDimensions(importPds.Samples(), outputLines, importPds.Bands());
    g_oCube->create(ui.GetFileName(outputParamName));
    g_oCube->addCachingAlgorithm(new BoxcarCachingAlgorithm());

    g_oBuff = new Isis::Brick(importPds.Samples(), outputLines, importPds.Bands(),
                              importPds.Samples(), 1, 1, importPds.PixelType(), true);
    g_oBuff->setpos(0);

    if (fileType == ProcessImportPds::L0) {
      importPds.StartProcess(writeCube);
    }
    else {
      importPds.StartProcess(writeCubeWithDroppedLines);
      g_results += PvlKeyword("LinesAdded", toString(g_totalLinesAdded));
      g_results += PvlKeyword("LinesFlipped", toString(linesNeedFlipped));
      g_results += PvlKeyword("SamplesFlipped", toString(samplesNeedFlipped));
    }

    delete g_oBuff;

    // If the image lines need flipped then so does the UTC table, if it exists.
    if (fileType != ProcessImportPds::L0) {
      if (linesNeedFlipped) {
        flipUtcTable(*g_utcTable);
      }
    }

    translateChandrayaan1M3Labels(pdsLabel, g_oCube, *g_utcTable, fileType); 

    if (fileType != ProcessImportPds::L0) g_oCube->write(*g_utcTable);

    importPds.WriteHistory(*g_oCube);
    importPds.Finalize();

    g_oCube->close();
    delete g_oCube;

  }

  CubeAttributeInput inAttribute;
  if (linesNeedFlipped) {
    ProcessBySample flipLines;
    flipLines.Progress()->SetText("Flipping Lines");
    Cube *cube = flipLines.SetInputCube(ui.GetFileName(outputParamName), inAttribute);
    cube->reopen("rw");
    flipLines.ProcessCubeInPlace(flip, false);
  }

  if (samplesNeedFlipped) {
    ProcessByLine flipSamples;
    flipSamples.Progress()->SetText("Flipping Samples");
    Cube *cube = flipSamples.SetInputCube(ui.GetFileName(outputParamName), inAttribute);
    cube->reopen("rw");
    flipSamples.ProcessCubeInPlace(flip, false);
  }
}


void writeCube(Buffer &in) {

  for (int i = 0; i < in.size(); i++) {
    (*g_oBuff)[i] = in[i];
  }

  g_oCube->write(*g_oBuff);
  (*g_oBuff)++;
}



void writeCubeWithDroppedLines(Buffer &in) {

  // Always write the current line to the output cube first
  for (int i = 0; i < in.size(); i++) {
    (*g_oBuff)[i] = in[i];
  }

  g_oCube->write(*g_oBuff);
  (*g_oBuff)++;

  // Now check the UTC_TIME table and see if there is a gap (missing lines) after the TIME record
  // for the current line. If there are, add as many line as are necessary to fill the gap. 
  // Since the PDS files are in BIL order we are writeing to the ISIS cube in that order, so we 
  // do not need to write NULL lines to fill a gap until we have written the last band of line N,
  // and we don't have to check for gaps after the last lines of the PDS file. 

  if (in.Band() == g_oCube->bandCount() && in.Line() < g_utcTable->Records()) {
    iTime thisEt((QString)(*g_utcTable)[in.Line() - 1]["UtcTime"]);
    iTime nextEt((QString)(*g_utcTable)[in.Line()]["UtcTime"]);

    double delta = fabs(nextEt - thisEt);

    double linesToAdd = (delta / g_expectedLineRate) - 1.0;

    if (linesToAdd > 0.9) {

      // Create a NULL line
      for (int i = 0; i < in.size(); i++) {
        (*g_oBuff)[i] = Null;
      }

//      qDebug() << "Lines to Add for this gap -----------------------------  " << linesToAdd;
//      qDebug() << qSetRealNumberPrecision(14) << "  ETs:  " << thisEt.Et() << nextEt.Et();
//      qDebug() << "  in buf pos: " << in.Sample() << in.Line() << in.Band();
//      qDebug() << "  obuf pos:   " << g_oBuff->Sample() << g_oBuff->Line() << g_oBuff->Band();

      while (linesToAdd > 0.9) {

        for (int band = 0; band < g_oCube->bandCount(); band++) {
          g_oCube->write(*g_oBuff); 
          (*g_oBuff)++;
        }
        linesToAdd--;
        g_totalLinesAdded++;
      }
    }
  }
}




void translateChandrayaan1M3Labels(Pvl& pdsLabel, Cube *ocube, Table& utcTable,
                                   ProcessImportPds::PdsFileType fileType) {
  Pvl outLabel;
  // Directory containing translation tables
  PvlGroup dataDir(Preference::Preferences().findGroup("DataDirectory"));
  QString transDir = (QString) dataDir["Chandrayaan1"] + "/translations/";

  // Translate the archive group
  FileName transFile(transDir + "m3Archive.trn");
  PvlTranslationManager archiveXlator(pdsLabel, transFile.expanded());
  archiveXlator.Auto(outLabel);
  ocube->putGroup(outLabel.findGroup("Archive", Pvl::Traverse));

  // Translate the instrument group
  transFile = transDir + "m3Instrument.trn";
  PvlTranslationManager instrumentXlator(pdsLabel, transFile.expanded());
  instrumentXlator.Auto(outLabel);

  PvlGroup &inst = outLabel.findGroup("Instrument", Pvl::Traverse);
  ocube->putGroup(inst);

 
  if (fileType == ProcessImportPds::L0 || fileType == ProcessImportPds::Rdn) {
    // Setup the band bin group
    QString bandFile = "$chandrayaan1/bandBin/bandBin.pvl";
    Pvl bandBinTemplate(bandFile);
    PvlObject modeObject = bandBinTemplate.findObject(pdsLabel["INSTRUMENT_MODE_ID"]);
    PvlGroup bandGroup = modeObject.findGroup("BandBin");
    //  Add OriginalBand
    int numBands;
    if ((QString)pdsLabel["INSTRUMENT_MODE_ID"] == "TARGET") {
      numBands = 256;
    }
    else {
      numBands = 85;
    }
    PvlKeyword originalBand("OriginalBand");
    for (int i = 1; i <= numBands; i++) {
      originalBand.addValue(toString(i));
    }
    bandGroup += originalBand;
    ocube->putGroup(bandGroup);

    if (fileType == ProcessImportPds::Rdn) {      
      // Setup the radiometric calibration group for the image cube
      PvlGroup calib("RadiometricCalibration");
      PvlKeyword solar = pdsLabel["SOLAR_DISTANCE"];
      calib += PvlKeyword("Units", "W/m2/um/sr");
      calib += PvlKeyword("SolarDistance", toString((double)solar), solar.unit());
      calib += PvlKeyword("DetectorTemperature", toString((double)pdsLabel["DETECTOR_TEMPERATURE"]));
      calib += PvlKeyword("SpectralCalibrationFileName",
                          (QString)pdsLabel["CH1:SPECTRAL_CALIBRATION_FILE_NAME"]);
      calib += PvlKeyword("RadGainFactorFileName",
                          (QString)pdsLabel["CH1:RAD_GAIN_FACTOR_FILE_NAME"]);
      calib += PvlKeyword("GlobalBandpassFileName",
                          (QString)pdsLabel["CH1:SPECTRAL_CALIBRATION_FILE_NAME"]);
      ocube->putGroup(calib);
    }
  }

  // Setup the band bin group
  else if (fileType == ProcessImportPds::Loc) {
    PvlGroup bandBin("BandBin");
    PvlKeyword bandNames = pdsLabel.findObject("LOC_IMAGE", PvlObject::Traverse)["BAND_NAME"];
    bandNames.setName("Name");
    bandBin += bandNames;
    PvlKeyword bandUnits("Units", "(Degrees, Degrees, Meters)");
    bandBin += bandUnits;
    ocube->putGroup(bandBin);
  }
  else if (fileType == ProcessImportPds::Obs) {
    PvlGroup bandBin("BandBin");
    PvlKeyword bandNames = pdsLabel.findObject("OBS_IMAGE", PvlObject::Traverse)["BAND_NAME"];
    bandNames.setName("Name");
    bandBin += bandNames;
    ocube->putGroup(bandBin);
  }

  // Setup the kernel group
  PvlGroup kern("Kernels");
  kern += PvlKeyword("NaifFrameCode", "-86520");
  ocube->putGroup(kern);

  OriginalLabel origLabel(pdsLabel);
  ocube->write(origLabel);
}


void flip(Buffer &in) {
  for(int i = 0; i < in.size() / 2; i++) {
    swap(in[i], in[in.size() - i - 1]);
  }
}


void flipUtcTable(Table &utcTable) {
  int nrecs = utcTable.Records();
  for (int i = 0; i < nrecs / 2; i++) {
    TableRecord rec1 = utcTable[i];
    TableRecord rec2 = utcTable[nrecs - i - 1];
    utcTable.Update(rec1, nrecs - i - 1);
    utcTable.Update(rec2, i);
  }
}


