#ifndef ControlNetVitals_h
#define ControlNetVitals_h
/**
 * @file
 * $Revision: 1.2 $
 * $Date: 2010/06/28 17:15:01 $
 *
 *   Unless noted otherwise, the portions of Isis written by the USGS are
 *   public domain. See individual third-party library and package descriptions
 *   for intellectual property information, user agreements, and related
 *   information.
 *
 *   Although Isis has been used by the USGS, no warranty, expressed or
 *   implied, is made by the USGS as to the accuracy and functioning of such
 *   software and related material nor shall the fact of distribution
 *   constitute any such warranty, and no responsibility is assumed by the
 *   USGS in connection therewith.
 *
 *   For additional information, launch
 *   $ISISROOT/doc/documents/Disclaimers/Disclaimers.html
 *   in a browser or see the Privacy &amp; Disclaimers page on the Isis website,
 *   http://isis.astrogeology.usgs.gov, and the USGS privacy and disclaimers on
 *   http://www.usgs.gov/privacy.html.
 */

#include <unordered_set>

#include <QStringList>

#include "ControlMeasure.h"
#include "ControlNet.h"
#include "ControlPoint.h"

namespace Isis {
  class ControlNet;


  /**
  * @author 2018-05-28 Adam Goins
  *
  * @internal
  *   @history 2018-05-28 Adam Goins - Initial Creation.
  */
  class ControlNetVitals : public QObject {
    Q_OBJECT

    public:
      ControlNetVitals(ControlNet *net);
      virtual ~ControlNetVitals();

      void initializeVitals();

      bool hasIslands();
      int numIslands();
      const QList< QList<QString> > &getIslands();

      int numPoints();
      int numIgnoredPoints();
      int numLockedPoints();
      int numFixedPoints();
      int numConstrainedPoints();
      int numFreePoints();
      int numPointsBelowMeasureThreshold(int num=3);

      int numImages();
      int numMeasures();
      int numImagesBelowMeasureThreshold(int num=3);
      int numImagesBelowHullTolerance(int tolerance=75);

      QList<QString> getCubeSerials();
      QList<ControlPoint*> getAllPoints();
      QList<ControlPoint*> getIgnoredPoints();
      QList<ControlPoint*> getLockedPoints();
      QList<ControlPoint*> getFixedPoints();
      QList<ControlPoint*> getConstrainedPoints();
      QList<ControlPoint*> getFreePoints();
      QList<ControlPoint*> getPointsBelowMeasureThreshold(int num=3);

      QList<QString> getAllImageSerials();
      QList<QString> getImagesBelowMeasureThreshold(int num=3);
      QList<QString> getImagesBelowHullTolerance(int num=75);

      QString getNetworkId();
      QString getStatus();
      QString getStatusDetails();
      void updateStatus(QString status, QString details);


      // ImageVitals getImageVitals(QString serial);

    signals:
      void networkChanged();

    public slots:
      void validate();
      void validateNetwork(ControlNet::ModType);
      void addPoint(ControlPoint *);
      void pointModified(ControlPoint *, ControlPoint::ModType, QVariant, QVariant);
      void deletePoint(ControlPoint *);
      void addMeasure(ControlMeasure *);
      void measureModified(ControlMeasure *, ControlMeasure::ModType, QVariant, QVariant);
      void deleteMeasure(ControlMeasure *);


    private:
      ControlNet *m_controlNet;

      QString m_status;
      QString m_statusDetails;

      QList< QList< QString > > m_islandList;

      QMap<int, int> m_pointMeasureCounts;
      QMap<int, int> m_imageMeasureCounts;
      QMap<ControlPoint::PointType, int> m_pointTypeCounts;

      int m_numPointsIgnored;
      int m_numPointsLocked;


      // QHash<QString, ImageVitals> m_imageVitals;

      // class ImageVitals {
      //   public:
      //     ImageVitals(QString cubeSerial,
      //                 QList<ControlMeasure*> measures,
      //                 QList<ControlMeasure*> validMeasures) {
      //       m_serial = cubeSerial;
      //       m_measures = measures;
      //       m_validMeasures = validMeasures;
      //     }
      //     ~ImageVitals() {}
      //
      //     QString getSerial {
      //       return m_serial;
      //     }
      //
      //     QList<ControlMeasure> getMeasures() {
      //       return m_measures;
      //     };
      //
      //     QList<ControlMeasure> getValidMeasures() {
      //       return m_validMeasures;
      //     }
      //
      //
      //
      //   private:
      //     QString m_serial;
      //     QList<ControlMeasure*> m_measures;
      //     QList<ControlMeasure*> m_validMeasures;
      //     ControlNet *m_controlNet;
      // };
  };
};

#endif
