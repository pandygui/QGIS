/***************************************************************************
  qgsgeometry.cpp - Geometry (stored as Open Geospatial Consortium WKB)
  -------------------------------------------------------------------
Date                 : 02 May 2005
Copyright            : (C) 2005 by Brendan Morley
email                : morb at ozemail dot com dot au
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <limits>
#include <cstdarg>
#include <cstdio>
#include <cmath>

#include "qgis.h"
#include "qgsgeometry.h"
#include "qgsgeometryeditutils.h"
#include "qgsgeometryfactory.h"
#include "qgsgeometrymakevalid.h"
#include "qgsgeometryutils.h"
#include "qgsinternalgeometryengine.h"
#include "qgsgeos.h"
#include "qgsapplication.h"
#include "qgslogger.h"
#include "qgsmaptopixel.h"
#include "qgsmessagelog.h"
#include "qgspointxy.h"
#include "qgsrectangle.h"

#include "qgsvectorlayer.h"
#include "qgsgeometryvalidator.h"

#include "qgsmulticurve.h"
#include "qgsmultilinestring.h"
#include "qgsmultipoint.h"
#include "qgsmultipolygon.h"
#include "qgsmultisurface.h"
#include "qgspoint.h"
#include "qgspolygon.h"
#include "qgslinestring.h"
#include "qgscircle.h"

struct QgsGeometryPrivate
{
  QgsGeometryPrivate(): ref( 1 ) {}
  QAtomicInt ref;
  std::unique_ptr< QgsAbstractGeometry > geometry;
};

QgsGeometry::QgsGeometry()
  : d( new QgsGeometryPrivate() )
{
}

QgsGeometry::~QgsGeometry()
{
  if ( !d->ref.deref() )
    delete d;
}

QgsGeometry::QgsGeometry( QgsAbstractGeometry *geom )
  : d( new QgsGeometryPrivate() )
{
  d->geometry.reset( geom );
  d->ref = QAtomicInt( 1 );
}

QgsGeometry::QgsGeometry( std::unique_ptr<QgsAbstractGeometry> geom )
  : d( new QgsGeometryPrivate() )
{
  d->geometry = std::move( geom );
  d->ref = QAtomicInt( 1 );
}

QgsGeometry::QgsGeometry( const QgsGeometry &other )
{
  d = other.d;
  mLastError = other.mLastError;
  d->ref.ref();
}

QgsGeometry &QgsGeometry::operator=( QgsGeometry const &other )
{
  if ( !d->ref.deref() )
  {
    delete d;
  }

  mLastError = other.mLastError;
  d = other.d;
  d->ref.ref();
  return *this;
}

void QgsGeometry::detach()
{
  if ( d->ref <= 1 )
    return;

  std::unique_ptr< QgsAbstractGeometry > cGeom;
  if ( d->geometry )
    cGeom.reset( d->geometry->clone() );

  reset( std::move( cGeom ) );
}

void QgsGeometry::reset( std::unique_ptr<QgsAbstractGeometry> newGeometry )
{
  if ( d->ref > 1 )
  {
    ( void )d->ref.deref();
    d = new QgsGeometryPrivate();
  }
  d->geometry = std::move( newGeometry );
}

QgsAbstractGeometry *QgsGeometry::geometry() const
{
  return d->geometry.get();
}

void QgsGeometry::setGeometry( QgsAbstractGeometry *geometry )
{
  if ( d->geometry.get() == geometry )
  {
    return;
  }

  reset( std::unique_ptr< QgsAbstractGeometry >( geometry ) );
}

bool QgsGeometry::isNull() const
{
  return !d->geometry;
}

QgsGeometry QgsGeometry::fromWkt( const QString &wkt )
{
  std::unique_ptr< QgsAbstractGeometry > geom = QgsGeometryFactory::geomFromWkt( wkt );
  if ( !geom )
  {
    return QgsGeometry();
  }
  return QgsGeometry( std::move( geom ) );
}

QgsGeometry QgsGeometry::fromPoint( const QgsPointXY &point )
{
  std::unique_ptr< QgsAbstractGeometry > geom( QgsGeometryFactory::fromPoint( point ) );
  if ( geom )
  {
    return QgsGeometry( geom.release() );
  }
  return QgsGeometry();
}

QgsGeometry QgsGeometry::fromPolyline( const QgsPolyline &polyline )
{
  std::unique_ptr< QgsAbstractGeometry > geom = QgsGeometryFactory::fromPolyline( polyline );
  if ( geom )
  {
    return QgsGeometry( std::move( geom ) );
  }
  return QgsGeometry();
}

QgsGeometry QgsGeometry::fromPolygon( const QgsPolygon &polygon )
{
  std::unique_ptr< QgsPolygonV2 > geom = QgsGeometryFactory::fromPolygon( polygon );
  if ( geom )
  {
    return QgsGeometry( std::move( geom.release() ) );
  }
  return QgsGeometry();
}

QgsGeometry QgsGeometry::fromMultiPoint( const QgsMultiPoint &multipoint )
{
  std::unique_ptr< QgsMultiPointV2 > geom = QgsGeometryFactory::fromMultiPoint( multipoint );
  if ( geom )
  {
    return QgsGeometry( std::move( geom ) );
  }
  return QgsGeometry();
}

QgsGeometry QgsGeometry::fromMultiPolyline( const QgsMultiPolyline &multiline )
{
  std::unique_ptr< QgsMultiLineString > geom = QgsGeometryFactory::fromMultiPolyline( multiline );
  if ( geom )
  {
    return QgsGeometry( std::move( geom ) );
  }
  return QgsGeometry();
}

QgsGeometry QgsGeometry::fromMultiPolygon( const QgsMultiPolygon &multipoly )
{
  std::unique_ptr< QgsMultiPolygonV2 > geom = QgsGeometryFactory::fromMultiPolygon( multipoly );
  if ( geom )
  {
    return QgsGeometry( std::move( geom ) );
  }
  return QgsGeometry();
}

QgsGeometry QgsGeometry::fromRect( const QgsRectangle &rect )
{
  QgsPolyline ring;
  ring.append( QgsPointXY( rect.xMinimum(), rect.yMinimum() ) );
  ring.append( QgsPointXY( rect.xMaximum(), rect.yMinimum() ) );
  ring.append( QgsPointXY( rect.xMaximum(), rect.yMaximum() ) );
  ring.append( QgsPointXY( rect.xMinimum(), rect.yMaximum() ) );
  ring.append( QgsPointXY( rect.xMinimum(), rect.yMinimum() ) );

  QgsPolygon polygon;
  polygon.append( ring );

  return fromPolygon( polygon );
}

QgsGeometry QgsGeometry::collectGeometry( const QList< QgsGeometry > &geometries )
{
  QgsGeometry collected;

  for ( const QgsGeometry &g : geometries )
  {
    if ( collected.isNull() )
    {
      collected = g;
      collected.convertToMultiType();
    }
    else
    {
      collected.addPart( g );
    }
  }
  return collected;
}

void QgsGeometry::fromWkb( unsigned char *wkb, int length )
{
  QgsConstWkbPtr ptr( wkb, length );
  reset( QgsGeometryFactory::geomFromWkb( ptr ) );
  delete [] wkb;
}

void QgsGeometry::fromWkb( const QByteArray &wkb )
{
  QgsConstWkbPtr ptr( wkb );
  reset( QgsGeometryFactory::geomFromWkb( ptr ) );
}

GEOSGeometry *QgsGeometry::exportToGeos( double precision ) const
{
  if ( !d->geometry )
  {
    return nullptr;
  }

  return QgsGeos::asGeos( d->geometry.get(), precision );
}


QgsWkbTypes::Type QgsGeometry::wkbType() const
{
  if ( !d->geometry )
  {
    return QgsWkbTypes::Unknown;
  }
  else
  {
    return d->geometry->wkbType();
  }
}


QgsWkbTypes::GeometryType QgsGeometry::type() const
{
  if ( !d->geometry )
  {
    return QgsWkbTypes::UnknownGeometry;
  }
  return static_cast< QgsWkbTypes::GeometryType >( QgsWkbTypes::geometryType( d->geometry->wkbType() ) );
}

bool QgsGeometry::isEmpty() const
{
  if ( !d->geometry )
  {
    return true;
  }

  return d->geometry->isEmpty();
}

bool QgsGeometry::isMultipart() const
{
  if ( !d->geometry )
  {
    return false;
  }
  return QgsWkbTypes::isMultiType( d->geometry->wkbType() );
}

void QgsGeometry::fromGeos( GEOSGeometry *geos )
{
  reset( QgsGeos::fromGeos( geos ) );
  GEOSGeom_destroy_r( QgsGeos::getGEOSHandler(), geos );
}

QgsPointXY QgsGeometry::closestVertex( const QgsPointXY &point, int &atVertex, int &beforeVertex, int &afterVertex, double &sqrDist ) const
{
  if ( !d->geometry )
  {
    sqrDist = -1;
    return QgsPointXY( 0, 0 );
  }

  QgsPoint pt( point.x(), point.y() );
  QgsVertexId id;

  QgsPoint vp = QgsGeometryUtils::closestVertex( *( d->geometry ), pt, id );
  if ( !id.isValid() )
  {
    sqrDist = -1;
    return QgsPointXY( 0, 0 );
  }
  sqrDist = QgsGeometryUtils::sqrDistance2D( pt, vp );

  atVertex = vertexNrFromVertexId( id );
  adjacentVertices( atVertex, beforeVertex, afterVertex );
  return QgsPointXY( vp.x(), vp.y() );
}

double QgsGeometry::distanceToVertex( int vertex ) const
{
  if ( !d->geometry )
  {
    return -1;
  }

  QgsVertexId id;
  if ( !vertexIdFromVertexNr( vertex, id ) )
  {
    return -1;
  }

  return QgsGeometryUtils::distanceToVertex( *( d->geometry ), id );
}

double QgsGeometry::angleAtVertex( int vertex ) const
{
  if ( !d->geometry )
  {
    return 0;
  }

  QgsVertexId v2;
  if ( !vertexIdFromVertexNr( vertex, v2 ) )
  {
    return 0;
  }

  QgsVertexId v1;
  QgsVertexId v3;
  QgsGeometryUtils::adjacentVertices( *d->geometry, v2, v1, v3 );
  if ( v1.isValid() && v3.isValid() )
  {
    QgsPoint p1 = d->geometry->vertexAt( v1 );
    QgsPoint p2 = d->geometry->vertexAt( v2 );
    QgsPoint p3 = d->geometry->vertexAt( v3 );
    double angle1 = QgsGeometryUtils::lineAngle( p1.x(), p1.y(), p2.x(), p2.y() );
    double angle2 = QgsGeometryUtils::lineAngle( p2.x(), p2.y(), p3.x(), p3.y() );
    return QgsGeometryUtils::averageAngle( angle1, angle2 );
  }
  else if ( v3.isValid() )
  {
    QgsPoint p1 = d->geometry->vertexAt( v2 );
    QgsPoint p2 = d->geometry->vertexAt( v3 );
    return QgsGeometryUtils::lineAngle( p1.x(), p1.y(), p2.x(), p2.y() );
  }
  else if ( v1.isValid() )
  {
    QgsPoint p1 = d->geometry->vertexAt( v1 );
    QgsPoint p2 = d->geometry->vertexAt( v2 );
    return QgsGeometryUtils::lineAngle( p1.x(), p1.y(), p2.x(), p2.y() );
  }
  return 0.0;
}

void QgsGeometry::adjacentVertices( int atVertex, int &beforeVertex, int &afterVertex ) const
{
  if ( !d->geometry )
  {
    return;
  }

  QgsVertexId id;
  if ( !vertexIdFromVertexNr( atVertex, id ) )
  {
    beforeVertex = -1;
    afterVertex = -1;
    return;
  }

  QgsVertexId beforeVertexId, afterVertexId;
  QgsGeometryUtils::adjacentVertices( *( d->geometry ), id, beforeVertexId, afterVertexId );
  beforeVertex = vertexNrFromVertexId( beforeVertexId );
  afterVertex = vertexNrFromVertexId( afterVertexId );
}

bool QgsGeometry::moveVertex( double x, double y, int atVertex )
{
  if ( !d->geometry )
  {
    return false;
  }

  QgsVertexId id;
  if ( !vertexIdFromVertexNr( atVertex, id ) )
  {
    return false;
  }

  detach();

  return d->geometry->moveVertex( id, QgsPoint( x, y ) );
}

bool QgsGeometry::moveVertex( const QgsPoint &p, int atVertex )
{
  if ( !d->geometry )
  {
    return false;
  }

  QgsVertexId id;
  if ( !vertexIdFromVertexNr( atVertex, id ) )
  {
    return false;
  }

  detach();

  return d->geometry->moveVertex( id, p );
}

bool QgsGeometry::deleteVertex( int atVertex )
{
  if ( !d->geometry )
  {
    return false;
  }

  //maintain compatibility with < 2.10 API
  if ( QgsWkbTypes::flatType( d->geometry->wkbType() ) == QgsWkbTypes::MultiPoint )
  {
    detach();
    //delete geometry instead of point
    return static_cast< QgsGeometryCollection * >( d->geometry.get() )->removeGeometry( atVertex );
  }

  //if it is a point, set the geometry to nullptr
  if ( QgsWkbTypes::flatType( d->geometry->wkbType() ) == QgsWkbTypes::Point )
  {
    reset( nullptr );
    return true;
  }

  QgsVertexId id;
  if ( !vertexIdFromVertexNr( atVertex, id ) )
  {
    return false;
  }

  detach();

  return d->geometry->deleteVertex( id );
}

bool QgsGeometry::insertVertex( double x, double y, int beforeVertex )
{
  if ( !d->geometry )
  {
    return false;
  }

  //maintain compatibility with < 2.10 API
  if ( QgsWkbTypes::flatType( d->geometry->wkbType() ) == QgsWkbTypes::MultiPoint )
  {
    detach();
    //insert geometry instead of point
    return static_cast< QgsGeometryCollection * >( d->geometry.get() )->insertGeometry( new QgsPoint( x, y ), beforeVertex );
  }

  QgsVertexId id;
  if ( !vertexIdFromVertexNr( beforeVertex, id ) )
  {
    return false;
  }

  detach();

  return d->geometry->insertVertex( id, QgsPoint( x, y ) );
}

bool QgsGeometry::insertVertex( const QgsPoint &point, int beforeVertex )
{
  if ( !d->geometry )
  {
    return false;
  }

  //maintain compatibility with < 2.10 API
  if ( QgsWkbTypes::flatType( d->geometry->wkbType() ) == QgsWkbTypes::MultiPoint )
  {
    detach();
    //insert geometry instead of point
    return static_cast< QgsGeometryCollection * >( d->geometry.get() )->insertGeometry( new QgsPoint( point ), beforeVertex );
  }

  QgsVertexId id;
  if ( !vertexIdFromVertexNr( beforeVertex, id ) )
  {
    return false;
  }

  detach();

  return d->geometry->insertVertex( id, point );
}

QgsPoint QgsGeometry::vertexAt( int atVertex ) const
{
  if ( !d->geometry )
  {
    return QgsPoint();
  }

  QgsVertexId vId;
  ( void )vertexIdFromVertexNr( atVertex, vId );
  if ( vId.vertex < 0 )
  {
    return QgsPoint();
  }
  return d->geometry->vertexAt( vId );
}

double QgsGeometry::sqrDistToVertexAt( QgsPointXY &point, int atVertex ) const
{
  QgsPointXY vertexPoint = vertexAt( atVertex );
  return QgsGeometryUtils::sqrDistance2D( QgsPoint( vertexPoint.x(), vertexPoint.y() ), QgsPoint( point.x(), point.y() ) );
}

QgsGeometry QgsGeometry::nearestPoint( const QgsGeometry &other ) const
{
  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  QgsGeometry result = geos.closestPoint( other );
  result.mLastError = mLastError;
  return result;
}

QgsGeometry QgsGeometry::shortestLine( const QgsGeometry &other ) const
{
  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  QgsGeometry result = geos.shortestLine( other, &mLastError );
  result.mLastError = mLastError;
  return result;
}

double QgsGeometry::closestVertexWithContext( const QgsPointXY &point, int &atVertex ) const
{
  if ( !d->geometry )
  {
    return -1;
  }

  QgsVertexId vId;
  QgsPoint pt( point.x(), point.y() );
  QgsPoint closestPoint = QgsGeometryUtils::closestVertex( *( d->geometry ), pt, vId );
  if ( !vId.isValid() )
    return -1;
  atVertex = vertexNrFromVertexId( vId );
  return QgsGeometryUtils::sqrDistance2D( closestPoint, pt );
}

double QgsGeometry::closestSegmentWithContext(
  const QgsPointXY &point,
  QgsPointXY &minDistPoint,
  int &afterVertex,
  double *leftOf,
  double epsilon ) const
{
  if ( !d->geometry )
  {
    return -1;
  }

  QgsPoint segmentPt;
  QgsVertexId vertexAfter;
  bool leftOfBool;

  double sqrDist = d->geometry->closestSegment( QgsPoint( point.x(), point.y() ), segmentPt,  vertexAfter, &leftOfBool, epsilon );
  if ( sqrDist < 0 )
    return -1;

  minDistPoint.setX( segmentPt.x() );
  minDistPoint.setY( segmentPt.y() );
  afterVertex = vertexNrFromVertexId( vertexAfter );
  if ( leftOf )
  {
    *leftOf = leftOfBool ? 1.0 : -1.0;
  }
  return sqrDist;
}

QgsGeometry::OperationResult QgsGeometry::addRing( const QList<QgsPointXY> &ring )
{
  std::unique_ptr< QgsLineString > ringLine = qgis::make_unique< QgsLineString >( ring );
  return addRing( ringLine.release() );
}

QgsGeometry::OperationResult QgsGeometry::addRing( QgsCurve *ring )
{
  std::unique_ptr< QgsCurve > r( ring );
  if ( !d->geometry )
  {
    return InvalidInput;
  }

  detach();

  return QgsGeometryEditUtils::addRing( d->geometry.get(), std::move( r ) );
}

QgsGeometry::OperationResult QgsGeometry::addPart( const QList<QgsPointXY> &points, QgsWkbTypes::GeometryType geomType )
{
  QgsPointSequence l;
  convertPointList( points, l );
  return addPart( l, geomType );
}

QgsGeometry::OperationResult QgsGeometry::addPart( const QgsPointSequence &points, QgsWkbTypes::GeometryType geomType )
{
  std::unique_ptr< QgsAbstractGeometry > partGeom;
  if ( points.size() == 1 )
  {
    partGeom = qgis::make_unique< QgsPoint >( points[0] );
  }
  else if ( points.size() > 1 )
  {
    std::unique_ptr< QgsLineString > ringLine = qgis::make_unique< QgsLineString >();
    ringLine->setPoints( points );
    partGeom = std::move( ringLine );
  }
  return addPart( partGeom.release(), geomType );
}

QgsGeometry::OperationResult QgsGeometry::addPart( QgsAbstractGeometry *part, QgsWkbTypes::GeometryType geomType )
{
  std::unique_ptr< QgsAbstractGeometry > p( part );
  if ( !d->geometry )
  {
    switch ( geomType )
    {
      case QgsWkbTypes::PointGeometry:
        reset( qgis::make_unique< QgsMultiPointV2 >() );
        break;
      case QgsWkbTypes::LineGeometry:
        reset( qgis::make_unique< QgsMultiLineString >() );
        break;
      case QgsWkbTypes::PolygonGeometry:
        reset( qgis::make_unique< QgsMultiPolygonV2 >() );
        break;
      default:
        reset( nullptr );
        return QgsGeometry::AddPartNotMultiGeometry;
    }
  }
  else
  {
    detach();
  }

  convertToMultiType();
  return QgsGeometryEditUtils::addPart( d->geometry.get(), std::move( p ) );
}

QgsGeometry::OperationResult QgsGeometry::addPart( const QgsGeometry &newPart )
{
  if ( !d->geometry )
  {
    return QgsGeometry::InvalidBaseGeometry;
  }
  if ( !newPart || !newPart.d->geometry )
  {
    return QgsGeometry::AddPartNotMultiGeometry;
  }

  return addPart( newPart.d->geometry->clone() );
}

QgsGeometry QgsGeometry::removeInteriorRings( double minimumRingArea ) const
{
  if ( !d->geometry || type() != QgsWkbTypes::PolygonGeometry )
  {
    return QgsGeometry();
  }

  if ( QgsWkbTypes::isMultiType( d->geometry->wkbType() ) )
  {
    const QList<QgsGeometry> parts = asGeometryCollection();
    QList<QgsGeometry> results;
    for ( const QgsGeometry &part : parts )
    {
      QgsGeometry result = part.removeInteriorRings( minimumRingArea );
      if ( result )
        results << result;
    }
    if ( results.isEmpty() )
      return QgsGeometry();

    QgsGeometry first = results.takeAt( 0 );
    for ( const QgsGeometry &result : qgis::as_const( results ) )
    {
      first.addPart( result );
    }
    return first;
  }
  else
  {
    std::unique_ptr< QgsCurvePolygon > newPoly( static_cast< QgsCurvePolygon * >( d->geometry->clone() ) );
    newPoly->removeInteriorRings( minimumRingArea );
    return QgsGeometry( std::move( newPoly ) );
  }
}

QgsGeometry::OperationResult QgsGeometry::addPart( GEOSGeometry *newPart )
{
  if ( !d->geometry )
  {
    return QgsGeometry::InvalidBaseGeometry;
  }
  if ( !newPart )
  {
    return QgsGeometry::AddPartNotMultiGeometry;
  }

  detach();

  std::unique_ptr< QgsAbstractGeometry > geom = QgsGeos::fromGeos( newPart );
  return QgsGeometryEditUtils::addPart( d->geometry.get(), std::move( geom ) );
}

QgsGeometry::OperationResult QgsGeometry::translate( double dx, double dy )
{
  if ( !d->geometry )
  {
    return QgsGeometry::InvalidBaseGeometry;
  }

  detach();

  d->geometry->transform( QTransform::fromTranslate( dx, dy ) );
  return QgsGeometry::Success;
}

QgsGeometry::OperationResult QgsGeometry::rotate( double rotation, const QgsPointXY &center )
{
  if ( !d->geometry )
  {
    return QgsGeometry::InvalidBaseGeometry;
  }

  detach();

  QTransform t = QTransform::fromTranslate( center.x(), center.y() );
  t.rotate( -rotation );
  t.translate( -center.x(), -center.y() );
  d->geometry->transform( t );
  return QgsGeometry::Success;
}

QgsGeometry::OperationResult QgsGeometry::splitGeometry( const QList<QgsPointXY> &splitLine, QList<QgsGeometry> &newGeometries, bool topological, QList<QgsPointXY> &topologyTestPoints )
{
  if ( !d->geometry )
  {
    return InvalidBaseGeometry;
  }

  QList<QgsGeometry > newGeoms;
  QgsLineString splitLineString( splitLine );
  QgsPointSequence tp;

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  QgsGeometryEngine::EngineOperationResult result = geos.splitGeometry( splitLineString, newGeoms, topological, tp, &mLastError );

  if ( result == QgsGeometryEngine::Success )
  {
    *this = newGeoms.takeAt( 0 );

    newGeometries = newGeoms;
  }

  convertPointList( tp, topologyTestPoints );

  switch ( result )
  {
    case QgsGeometryEngine::Success:
      return QgsGeometry::Success;
    case QgsGeometryEngine::MethodNotImplemented:
    case QgsGeometryEngine::EngineError:
    case QgsGeometryEngine::NodedGeometryError:
      return QgsGeometry::GeometryEngineError;
    case QgsGeometryEngine::InvalidBaseGeometry:
      return QgsGeometry::InvalidBaseGeometry;
    case QgsGeometryEngine::InvalidInput:
      return QgsGeometry::InvalidInput;
    case QgsGeometryEngine::SplitCannotSplitPoint:
      return QgsGeometry::SplitCannotSplitPoint;
    case QgsGeometryEngine::NothingHappened:
      return QgsGeometry::NothingHappened;
      //default: do not implement default to handle properly all cases
  }

  // this should never be reached
  Q_ASSERT( false );
  return QgsGeometry::NothingHappened;
}

QgsGeometry::OperationResult QgsGeometry::reshapeGeometry( const QgsLineString &reshapeLineString )
{
  if ( !d->geometry )
  {
    return InvalidBaseGeometry;
  }

  QgsGeos geos( d->geometry.get() );
  QgsGeometryEngine::EngineOperationResult errorCode = QgsGeometryEngine::Success;
  mLastError.clear();
  std::unique_ptr< QgsAbstractGeometry > geom( geos.reshapeGeometry( reshapeLineString, &errorCode, &mLastError ) );
  if ( errorCode == QgsGeometryEngine::Success && geom )
  {
    reset( std::move( geom ) );
    return Success;
  }

  switch ( errorCode )
  {
    case QgsGeometryEngine::Success:
      return Success;
    case QgsGeometryEngine::MethodNotImplemented:
    case QgsGeometryEngine::EngineError:
    case QgsGeometryEngine::NodedGeometryError:
      return GeometryEngineError;
    case QgsGeometryEngine::InvalidBaseGeometry:
      return InvalidBaseGeometry;
    case QgsGeometryEngine::InvalidInput:
      return InvalidInput;
    case QgsGeometryEngine::SplitCannotSplitPoint: // should not happen
      return GeometryEngineError;
    case QgsGeometryEngine::NothingHappened:
      return NothingHappened;
  }

  // should not be reached
  return GeometryEngineError;
}

int QgsGeometry::makeDifferenceInPlace( const QgsGeometry &other )
{
  if ( !d->geometry || !other.d->geometry )
  {
    return 0;
  }

  QgsGeos geos( d->geometry.get() );

  mLastError.clear();
  std::unique_ptr< QgsAbstractGeometry > diffGeom( geos.intersection( other.geometry(), &mLastError ) );
  if ( !diffGeom )
  {
    return 1;
  }

  reset( std::move( diffGeom ) );
  return 0;
}

QgsGeometry QgsGeometry::makeDifference( const QgsGeometry &other ) const
{
  if ( !d->geometry || other.isNull() )
  {
    return QgsGeometry();
  }

  QgsGeos geos( d->geometry.get() );

  mLastError.clear();
  std::unique_ptr< QgsAbstractGeometry > diffGeom( geos.intersection( other.geometry(), &mLastError ) );
  if ( !diffGeom )
  {
    QgsGeometry result;
    result.mLastError = mLastError;
    return result;
  }

  return QgsGeometry( diffGeom.release() );
}

QgsRectangle QgsGeometry::boundingBox() const
{
  if ( d->geometry )
  {
    return d->geometry->boundingBox();
  }
  return QgsRectangle();
}

QgsGeometry QgsGeometry::orientedMinimumBoundingBox( double &area, double &angle, double &width, double &height ) const
{
  QgsRectangle minRect;
  area = DBL_MAX;
  angle = 0;
  width = DBL_MAX;
  height = DBL_MAX;

  if ( !d->geometry || d->geometry->nCoordinates() < 2 )
    return QgsGeometry();

  QgsGeometry hull = convexHull();
  if ( hull.isNull() )
    return QgsGeometry();

  QgsVertexId vertexId;
  QgsPoint pt0;
  QgsPoint pt1;
  QgsPoint pt2;
  // get first point
  hull.geometry()->nextVertex( vertexId, pt0 );
  pt1 = pt0;
  double prevAngle = 0.0;
  while ( hull.geometry()->nextVertex( vertexId, pt2 ) )
  {
    double currentAngle = QgsGeometryUtils::lineAngle( pt1.x(), pt1.y(), pt2.x(), pt2.y() );
    double rotateAngle = 180.0 / M_PI * ( currentAngle - prevAngle );
    prevAngle = currentAngle;

    QTransform t = QTransform::fromTranslate( pt0.x(), pt0.y() );
    t.rotate( rotateAngle );
    t.translate( -pt0.x(), -pt0.y() );

    hull.geometry()->transform( t );

    QgsRectangle bounds = hull.geometry()->boundingBox();
    double currentArea = bounds.width() * bounds.height();
    if ( currentArea  < area )
    {
      minRect = bounds;
      area = currentArea;
      angle = 180.0 / M_PI * currentAngle;
      width = bounds.width();
      height = bounds.height();
    }

    pt2 = pt1;
  }

  QgsGeometry minBounds = QgsGeometry::fromRect( minRect );
  minBounds.rotate( angle, QgsPointXY( pt0.x(), pt0.y() ) );

  // constrain angle to 0 - 180
  if ( angle > 180.0 )
    angle = std::fmod( angle, 180.0 );

  return minBounds;
}

QgsGeometry QgsGeometry::orientedMinimumBoundingBox() const
{
  double area, angle, width, height;
  return orientedMinimumBoundingBox( area, angle, width, height );
}

static QgsCircle __recMinimalEnclosingCircle( QgsMultiPoint points, QgsMultiPoint boundary )
{
  auto l_boundary = boundary.length();
  QgsCircle circ_mec;
  if ( ( points.length() == 0 ) || ( l_boundary == 3 ) )
  {
    switch ( l_boundary )
    {
      case 0:
        circ_mec = QgsCircle();
        break;
      case 1:
        circ_mec = QgsCircle( QgsPoint( boundary.last() ), 0 );
        boundary.pop_back();
        break;
      case 2:
      {
        QgsPointXY p1 = boundary.last();
        boundary.pop_back();
        QgsPointXY p2 = boundary.last();
        boundary.pop_back();
        circ_mec = QgsCircle().from2Points( QgsPoint( p1 ), QgsPoint( p2 ) );
      }
      break;
      default:
        QgsPoint p1( boundary.at( 0 ) );
        QgsPoint p2( boundary.at( 1 ) );
        QgsPoint p3( boundary.at( 2 ) );
        circ_mec = QgsCircle().minimalCircleFrom3Points( p1, p2, p3 );
        break;
    }
    return circ_mec;
  }
  else
  {
    QgsPointXY pxy = points.last();
    points.pop_back();
    circ_mec = __recMinimalEnclosingCircle( points, boundary );
    QgsPoint p( pxy );
    if ( !circ_mec.contains( p ) )
    {
      boundary.append( pxy );
      circ_mec = __recMinimalEnclosingCircle( points, boundary );
    }
  }
  return circ_mec;
}

QgsGeometry QgsGeometry::minimalEnclosingCircle( QgsPointXY &center, double &radius, unsigned int segments ) const
{
  center = QgsPointXY( );
  radius = 0;

  if ( !d->geometry )
  {
    return QgsGeometry();
  }

  /* optimization */
  QgsGeometry hull = convexHull();
  if ( hull.isNull() )
    return QgsGeometry();

  QgsMultiPoint P = hull.convertToPoint( true ).asMultiPoint();
  QgsMultiPoint R;

  QgsCircle circ = __recMinimalEnclosingCircle( P, R );
  center = QgsPointXY( circ.center() );
  radius = circ.radius();
  QgsGeometry geom;
  geom.setGeometry( circ.toPolygon( segments ) );
  return geom;

}

QgsGeometry QgsGeometry::minimalEnclosingCircle( unsigned int segments ) const
{
  QgsPointXY center;
  double radius;
  return minimalEnclosingCircle( center, radius, segments );

}

QgsGeometry QgsGeometry::orthogonalize( double tolerance, int maxIterations, double angleThreshold ) const
{
  QgsInternalGeometryEngine engine( *this );

  return engine.orthogonalize( tolerance, maxIterations, angleThreshold );
}

bool QgsGeometry::intersects( const QgsRectangle &r ) const
{
  QgsGeometry g = fromRect( r );
  return intersects( g );
}

bool QgsGeometry::intersects( const QgsGeometry &geometry ) const
{
  if ( !d->geometry || geometry.isNull() )
  {
    return false;
  }

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  return geos.intersects( geometry.d->geometry.get(), &mLastError );
}

bool QgsGeometry::contains( const QgsPointXY *p ) const
{
  if ( !d->geometry || !p )
  {
    return false;
  }

  QgsPoint pt( p->x(), p->y() );
  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  return geos.contains( &pt, &mLastError );
}

bool QgsGeometry::contains( const QgsGeometry &geometry ) const
{
  if ( !d->geometry || geometry.isNull() )
  {
    return false;
  }

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  return geos.contains( geometry.d->geometry.get(), &mLastError );
}

bool QgsGeometry::disjoint( const QgsGeometry &geometry ) const
{
  if ( !d->geometry || geometry.isNull() )
  {
    return false;
  }

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  return geos.disjoint( geometry.d->geometry.get(), &mLastError );
}

bool QgsGeometry::equals( const QgsGeometry &geometry ) const
{
  if ( !d->geometry || geometry.isNull() )
  {
    return false;
  }

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  return geos.isEqual( geometry.d->geometry.get(), &mLastError );
}

bool QgsGeometry::touches( const QgsGeometry &geometry ) const
{
  if ( !d->geometry || geometry.isNull() )
  {
    return false;
  }

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  return geos.touches( geometry.d->geometry.get(), &mLastError );
}

bool QgsGeometry::overlaps( const QgsGeometry &geometry ) const
{
  if ( !d->geometry || geometry.isNull() )
  {
    return false;
  }

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  return geos.overlaps( geometry.d->geometry.get(), &mLastError );
}

bool QgsGeometry::within( const QgsGeometry &geometry ) const
{
  if ( !d->geometry || geometry.isNull() )
  {
    return false;
  }

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  return geos.within( geometry.d->geometry.get(), &mLastError );
}

bool QgsGeometry::crosses( const QgsGeometry &geometry ) const
{
  if ( !d->geometry || geometry.isNull() )
  {
    return false;
  }

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  return geos.crosses( geometry.d->geometry.get(), &mLastError );
}

QString QgsGeometry::exportToWkt( int precision ) const
{
  if ( !d->geometry )
  {
    return QString();
  }
  return d->geometry->asWkt( precision );
}

QString QgsGeometry::exportToGeoJSON( int precision ) const
{
  if ( !d->geometry )
  {
    return QStringLiteral( "null" );
  }
  return d->geometry->asJSON( precision );
}

QgsGeometry QgsGeometry::convertToType( QgsWkbTypes::GeometryType destType, bool destMultipart ) const
{
  switch ( destType )
  {
    case QgsWkbTypes::PointGeometry:
      return convertToPoint( destMultipart );

    case QgsWkbTypes::LineGeometry:
      return convertToLine( destMultipart );

    case QgsWkbTypes::PolygonGeometry:
      return convertToPolygon( destMultipart );

    default:
      return QgsGeometry();
  }
}

bool QgsGeometry::convertToMultiType()
{
  if ( !d->geometry )
  {
    return false;
  }

  if ( isMultipart() ) //already multitype, no need to convert
  {
    return true;
  }

  std::unique_ptr< QgsAbstractGeometry >geom = QgsGeometryFactory::geomFromWkbType( QgsWkbTypes::multiType( d->geometry->wkbType() ) );
  QgsGeometryCollection *multiGeom = qgsgeometry_cast<QgsGeometryCollection *>( geom.get() );
  if ( !multiGeom )
  {
    return false;
  }

  multiGeom->addGeometry( d->geometry->clone() );
  reset( std::move( geom ) );
  return true;
}

bool QgsGeometry::convertToSingleType()
{
  if ( !d->geometry )
  {
    return false;
  }

  if ( !isMultipart() ) //already single part, no need to convert
  {
    return true;
  }

  QgsGeometryCollection *multiGeom = qgsgeometry_cast<QgsGeometryCollection *>( d->geometry.get() );
  if ( !multiGeom || multiGeom->partCount() < 1 )
    return false;

  std::unique_ptr< QgsAbstractGeometry > firstPart( multiGeom->geometryN( 0 )->clone() );
  reset( std::move( firstPart ) );
  return true;
}

QgsPointXY QgsGeometry::asPoint() const
{
  if ( !d->geometry || QgsWkbTypes::flatType( d->geometry->wkbType() ) != QgsWkbTypes::Point )
  {
    return QgsPointXY();
  }
  QgsPoint *pt = qgsgeometry_cast<QgsPoint *>( d->geometry.get() );
  if ( !pt )
  {
    return QgsPointXY();
  }

  return QgsPointXY( pt->x(), pt->y() );
}

QgsPolyline QgsGeometry::asPolyline() const
{
  QgsPolyline polyLine;
  if ( !d->geometry )
  {
    return polyLine;
  }

  bool doSegmentation = ( QgsWkbTypes::flatType( d->geometry->wkbType() ) == QgsWkbTypes::CompoundCurve
                          || QgsWkbTypes::flatType( d->geometry->wkbType() ) == QgsWkbTypes::CircularString );
  std::unique_ptr< QgsLineString > segmentizedLine;
  QgsLineString *line = nullptr;
  if ( doSegmentation )
  {
    QgsCurve *curve = qgsgeometry_cast<QgsCurve *>( d->geometry.get() );
    if ( !curve )
    {
      return polyLine;
    }
    segmentizedLine.reset( curve->curveToLine() );
    line = segmentizedLine.get();
  }
  else
  {
    line = qgsgeometry_cast<QgsLineString *>( d->geometry.get() );
    if ( !line )
    {
      return polyLine;
    }
  }

  int nVertices = line->numPoints();
  polyLine.resize( nVertices );
  for ( int i = 0; i < nVertices; ++i )
  {
    polyLine[i].setX( line->xAt( i ) );
    polyLine[i].setY( line->yAt( i ) );
  }

  return polyLine;
}

QgsPolygon QgsGeometry::asPolygon() const
{
  if ( !d->geometry )
    return QgsPolygon();

  bool doSegmentation = ( QgsWkbTypes::flatType( d->geometry->wkbType() ) == QgsWkbTypes::CurvePolygon );

  QgsPolygonV2 *p = nullptr;
  std::unique_ptr< QgsPolygonV2 > segmentized;
  if ( doSegmentation )
  {
    QgsCurvePolygon *curvePoly = qgsgeometry_cast<QgsCurvePolygon *>( d->geometry.get() );
    if ( !curvePoly )
    {
      return QgsPolygon();
    }
    segmentized.reset( curvePoly->toPolygon() );
    p = segmentized.get();
  }
  else
  {
    p = qgsgeometry_cast<QgsPolygonV2 *>( d->geometry.get() );
  }

  if ( !p )
  {
    return QgsPolygon();
  }

  QgsPolygon polygon;
  convertPolygon( *p, polygon );

  return polygon;
}

QgsMultiPoint QgsGeometry::asMultiPoint() const
{
  if ( !d->geometry || QgsWkbTypes::flatType( d->geometry->wkbType() ) != QgsWkbTypes::MultiPoint )
  {
    return QgsMultiPoint();
  }

  const QgsMultiPointV2 *mp = qgsgeometry_cast<QgsMultiPointV2 *>( d->geometry.get() );
  if ( !mp )
  {
    return QgsMultiPoint();
  }

  int nPoints = mp->numGeometries();
  QgsMultiPoint multiPoint( nPoints );
  for ( int i = 0; i < nPoints; ++i )
  {
    const QgsPoint *pt = static_cast<const QgsPoint *>( mp->geometryN( i ) );
    multiPoint[i].setX( pt->x() );
    multiPoint[i].setY( pt->y() );
  }
  return multiPoint;
}

QgsMultiPolyline QgsGeometry::asMultiPolyline() const
{
  if ( !d->geometry )
  {
    return QgsMultiPolyline();
  }

  QgsGeometryCollection *geomCollection = qgsgeometry_cast<QgsGeometryCollection *>( d->geometry.get() );
  if ( !geomCollection )
  {
    return QgsMultiPolyline();
  }

  int nLines = geomCollection->numGeometries();
  if ( nLines < 1 )
  {
    return QgsMultiPolyline();
  }

  QgsMultiPolyline mpl;
  for ( int i = 0; i < nLines; ++i )
  {
    const QgsLineString *line = qgsgeometry_cast<const QgsLineString *>( geomCollection->geometryN( i ) );
    std::unique_ptr< QgsLineString > segmentized;
    if ( !line )
    {
      const QgsCurve *curve = qgsgeometry_cast<const QgsCurve *>( geomCollection->geometryN( i ) );
      if ( !curve )
      {
        continue;
      }
      segmentized.reset( curve->curveToLine() );
      line = segmentized.get();
    }

    QgsPointSequence lineCoords;
    line->points( lineCoords );
    QgsPolyline polyLine;
    convertToPolyline( lineCoords, polyLine );
    mpl.append( polyLine );
  }
  return mpl;
}

QgsMultiPolygon QgsGeometry::asMultiPolygon() const
{
  if ( !d->geometry )
  {
    return QgsMultiPolygon();
  }

  QgsGeometryCollection *geomCollection = qgsgeometry_cast<QgsGeometryCollection *>( d->geometry.get() );
  if ( !geomCollection )
  {
    return QgsMultiPolygon();
  }

  int nPolygons = geomCollection->numGeometries();
  if ( nPolygons < 1 )
  {
    return QgsMultiPolygon();
  }

  QgsMultiPolygon mp;
  for ( int i = 0; i < nPolygons; ++i )
  {
    const QgsPolygonV2 *polygon = qgsgeometry_cast<const QgsPolygonV2 *>( geomCollection->geometryN( i ) );
    if ( !polygon )
    {
      const QgsCurvePolygon *cPolygon = qgsgeometry_cast<const QgsCurvePolygon *>( geomCollection->geometryN( i ) );
      if ( cPolygon )
      {
        polygon = cPolygon->toPolygon();
      }
      else
      {
        continue;
      }
    }

    QgsPolygon poly;
    convertPolygon( *polygon, poly );
    mp.append( poly );
  }
  return mp;
}

double QgsGeometry::area() const
{
  if ( !d->geometry )
  {
    return -1.0;
  }
  QgsGeos g( d->geometry.get() );

#if 0
  //debug: compare geos area with calculation in QGIS
  double geosArea = g.area();
  double qgisArea = 0;
  QgsSurface *surface = qgsgeometry_cast<QgsSurface *>( d->geometry );
  if ( surface )
  {
    qgisArea = surface->area();
  }
#endif

  mLastError.clear();
  return g.area( &mLastError );
}

double QgsGeometry::length() const
{
  if ( !d->geometry )
  {
    return -1.0;
  }
  QgsGeos g( d->geometry.get() );
  mLastError.clear();
  return g.length( &mLastError );
}

double QgsGeometry::distance( const QgsGeometry &geom ) const
{
  if ( !d->geometry || !geom.d->geometry )
  {
    return -1.0;
  }

  QgsGeos g( d->geometry.get() );
  mLastError.clear();
  return g.distance( geom.d->geometry.get(), &mLastError );
}

double QgsGeometry::hausdorffDistance( const QgsGeometry &geom ) const
{
  if ( !d->geometry || !geom.d->geometry )
  {
    return -1.0;
  }

  QgsGeos g( d->geometry.get() );
  mLastError.clear();
  return g.hausdorffDistance( geom.d->geometry.get(), &mLastError );
}

double QgsGeometry::hausdorffDistanceDensify( const QgsGeometry &geom, double densifyFraction ) const
{
  if ( !d->geometry || !geom.d->geometry )
  {
    return -1.0;
  }

  QgsGeos g( d->geometry.get() );
  mLastError.clear();
  return g.hausdorffDistanceDensify( geom.d->geometry.get(), densifyFraction, &mLastError );
}

QgsAbstractGeometry::vertex_iterator QgsGeometry::vertices_begin() const
{
  if ( !d->geometry )
    return QgsAbstractGeometry::vertex_iterator();
  return d->geometry->vertices_begin();
}

QgsAbstractGeometry::vertex_iterator QgsGeometry::vertices_end() const
{
  if ( !d->geometry )
    return QgsAbstractGeometry::vertex_iterator();
  return d->geometry->vertices_end();
}

QgsVertexIterator QgsGeometry::vertices() const
{
  if ( !d->geometry )
    return QgsVertexIterator();
  return QgsVertexIterator( d->geometry.get() );
}

QgsGeometry QgsGeometry::buffer( double distance, int segments ) const
{
  if ( !d->geometry )
  {
    return QgsGeometry();
  }

  QgsGeos g( d->geometry.get() );
  mLastError.clear();
  std::unique_ptr<QgsAbstractGeometry> geom( g.buffer( distance, segments, &mLastError ) );
  if ( !geom )
  {
    QgsGeometry result;
    result.mLastError = mLastError;
    return result;
  }
  return QgsGeometry( std::move( geom ) );
}

QgsGeometry QgsGeometry::buffer( double distance, int segments, EndCapStyle endCapStyle, JoinStyle joinStyle, double miterLimit ) const
{
  if ( !d->geometry )
  {
    return QgsGeometry();
  }

  QgsGeos g( d->geometry.get() );
  mLastError.clear();
  QgsAbstractGeometry *geom = g.buffer( distance, segments, endCapStyle, joinStyle, miterLimit, &mLastError );
  if ( !geom )
  {
    QgsGeometry result;
    result.mLastError = mLastError;
    return result;
  }
  return QgsGeometry( geom );
}

QgsGeometry QgsGeometry::offsetCurve( double distance, int segments, JoinStyle joinStyle, double miterLimit ) const
{
  if ( !d->geometry || type() != QgsWkbTypes::LineGeometry )
  {
    return QgsGeometry();
  }

  if ( QgsWkbTypes::isMultiType( d->geometry->wkbType() ) )
  {
    const QList<QgsGeometry> parts = asGeometryCollection();
    QList<QgsGeometry> results;
    for ( const QgsGeometry &part : parts )
    {
      QgsGeometry result = part.offsetCurve( distance, segments, joinStyle, miterLimit );
      if ( result )
        results << result;
    }
    if ( results.isEmpty() )
      return QgsGeometry();

    QgsGeometry first = results.takeAt( 0 );
    for ( const QgsGeometry &result : qgis::as_const( results ) )
    {
      first.addPart( result );
    }
    return first;
  }
  else
  {
    QgsGeos geos( d->geometry.get() );
    mLastError.clear();
    QgsAbstractGeometry *offsetGeom = geos.offsetCurve( distance, segments, joinStyle, miterLimit, &mLastError );
    if ( !offsetGeom )
    {
      QgsGeometry result;
      result.mLastError = mLastError;
      return result;
    }
    return QgsGeometry( offsetGeom );
  }
}

QgsGeometry QgsGeometry::singleSidedBuffer( double distance, int segments, BufferSide side, JoinStyle joinStyle, double miterLimit ) const
{
  if ( !d->geometry || type() != QgsWkbTypes::LineGeometry )
  {
    return QgsGeometry();
  }

  if ( QgsWkbTypes::isMultiType( d->geometry->wkbType() ) )
  {
    const QList<QgsGeometry> parts = asGeometryCollection();
    QList<QgsGeometry> results;
    for ( const QgsGeometry &part : parts )
    {
      QgsGeometry result = part.singleSidedBuffer( distance, segments, side, joinStyle, miterLimit );
      if ( result )
        results << result;
    }
    if ( results.isEmpty() )
      return QgsGeometry();

    QgsGeometry first = results.takeAt( 0 );
    for ( const QgsGeometry &result : qgis::as_const( results ) )
    {
      first.addPart( result );
    }
    return first;
  }
  else
  {
    QgsGeos geos( d->geometry.get() );
    mLastError.clear();
    std::unique_ptr< QgsAbstractGeometry > bufferGeom = geos.singleSidedBuffer( distance, segments, side,
        joinStyle, miterLimit, &mLastError );
    if ( !bufferGeom )
    {
      QgsGeometry result;
      result.mLastError = mLastError;
      return result;
    }
    return QgsGeometry( std::move( bufferGeom ) );
  }
}

QgsGeometry QgsGeometry::extendLine( double startDistance, double endDistance ) const
{
  if ( !d->geometry || type() != QgsWkbTypes::LineGeometry )
  {
    return QgsGeometry();
  }

  if ( QgsWkbTypes::isMultiType( d->geometry->wkbType() ) )
  {
    const QList<QgsGeometry> parts = asGeometryCollection();
    QList<QgsGeometry> results;
    for ( const QgsGeometry &part : parts )
    {
      QgsGeometry result = part.extendLine( startDistance, endDistance );
      if ( result )
        results << result;
    }
    if ( results.isEmpty() )
      return QgsGeometry();

    QgsGeometry first = results.takeAt( 0 );
    for ( const QgsGeometry &result : qgis::as_const( results ) )
    {
      first.addPart( result );
    }
    return first;
  }
  else
  {
    QgsLineString *line = qgsgeometry_cast< QgsLineString * >( d->geometry.get() );
    if ( !line )
      return QgsGeometry();

    std::unique_ptr< QgsLineString > newLine( line->clone() );
    newLine->extend( startDistance, endDistance );
    return QgsGeometry( std::move( newLine ) );
  }
}

QgsGeometry QgsGeometry::simplify( double tolerance ) const
{
  if ( !d->geometry )
  {
    return QgsGeometry();
  }

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  std::unique_ptr< QgsAbstractGeometry > simplifiedGeom( geos.simplify( tolerance, &mLastError ) );
  if ( !simplifiedGeom )
  {
    QgsGeometry result;
    result.mLastError = mLastError;
    return result;
  }
  return QgsGeometry( std::move( simplifiedGeom ) );
}

QgsGeometry QgsGeometry::densifyByCount( int extraNodesPerSegment ) const
{
  QgsInternalGeometryEngine engine( *this );

  return engine.densifyByCount( extraNodesPerSegment );
}

QgsGeometry QgsGeometry::densifyByDistance( double distance ) const
{
  QgsInternalGeometryEngine engine( *this );

  return engine.densifyByDistance( distance );
}

QgsGeometry QgsGeometry::centroid() const
{
  if ( !d->geometry )
  {
    return QgsGeometry();
  }

  QgsGeos geos( d->geometry.get() );

  mLastError.clear();
  QgsGeometry result( geos.centroid( &mLastError ) );
  result.mLastError = mLastError;
  return result;
}

QgsGeometry QgsGeometry::pointOnSurface() const
{
  if ( !d->geometry )
  {
    return QgsGeometry();
  }

  QgsGeos geos( d->geometry.get() );

  mLastError.clear();
  QgsGeometry result( geos.pointOnSurface( &mLastError ) );
  result.mLastError = mLastError;
  return result;
}

QgsGeometry QgsGeometry::poleOfInaccessibility( double precision, double *distanceToBoundary ) const
{
  QgsInternalGeometryEngine engine( *this );

  return engine.poleOfInaccessibility( precision, distanceToBoundary );
}

QgsGeometry QgsGeometry::convexHull() const
{
  if ( !d->geometry )
  {
    return QgsGeometry();
  }
  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  std::unique_ptr< QgsAbstractGeometry > cHull( geos.convexHull( &mLastError ) );
  if ( !cHull )
  {
    QgsGeometry geom;
    geom.mLastError = mLastError;
    return geom;
  }
  return QgsGeometry( std::move( cHull ) );
}

QgsGeometry QgsGeometry::voronoiDiagram( const QgsGeometry &extent, double tolerance, bool edgesOnly ) const
{
  if ( !d->geometry )
  {
    return QgsGeometry();
  }

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  QgsGeometry result = geos.voronoiDiagram( extent.geometry(), tolerance, edgesOnly, &mLastError );
  result.mLastError = mLastError;
  return result;
}

QgsGeometry QgsGeometry::delaunayTriangulation( double tolerance, bool edgesOnly ) const
{
  if ( !d->geometry )
  {
    return QgsGeometry();
  }

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  QgsGeometry result = geos.delaunayTriangulation( tolerance, edgesOnly );
  result.mLastError = mLastError;
  return result;
}

QgsGeometry QgsGeometry::subdivide( int maxNodes ) const
{
  if ( !d->geometry )
  {
    return QgsGeometry();
  }

  const QgsAbstractGeometry *geom = d->geometry.get();
  std::unique_ptr< QgsAbstractGeometry > segmentizedCopy;
  if ( QgsWkbTypes::isCurvedType( d->geometry->wkbType() ) )
  {
    segmentizedCopy.reset( d->geometry->segmentize() );
    geom = segmentizedCopy.get();
  }

  QgsGeos geos( geom );
  mLastError.clear();
  std::unique_ptr< QgsAbstractGeometry > result( geos.subdivide( maxNodes, &mLastError ) );
  if ( !result )
  {
    QgsGeometry geom;
    geom.mLastError = mLastError;
    return geom;
  }
  return QgsGeometry( std::move( result ) );
}

QgsGeometry QgsGeometry::interpolate( double distance ) const
{
  if ( !d->geometry )
  {
    return QgsGeometry();
  }

  QgsGeometry line = *this;
  if ( type() == QgsWkbTypes::PolygonGeometry )
    line = QgsGeometry( d->geometry->boundary() );

  QgsGeos geos( line.geometry() );
  mLastError.clear();
  std::unique_ptr< QgsAbstractGeometry > result( geos.interpolate( distance, &mLastError ) );
  if ( !result )
  {
    QgsGeometry geom;
    geom.mLastError = mLastError;
    return geom;
  }
  return QgsGeometry( std::move( result ) );
}

double QgsGeometry::lineLocatePoint( const QgsGeometry &point ) const
{
  if ( type() != QgsWkbTypes::LineGeometry )
    return -1;

  if ( QgsWkbTypes::flatType( point.wkbType() ) != QgsWkbTypes::Point )
    return -1;

  QgsGeometry segmentized = *this;
  if ( QgsWkbTypes::isCurvedType( wkbType() ) )
  {
    segmentized = QgsGeometry( static_cast< QgsCurve * >( d->geometry.get() )->segmentize() );
  }

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  return geos.lineLocatePoint( *( static_cast< QgsPoint * >( point.d->geometry.get() ) ), &mLastError );
}

double QgsGeometry::interpolateAngle( double distance ) const
{
  if ( !d->geometry )
    return 0.0;

  // always operate on segmentized geometries
  QgsGeometry segmentized = *this;
  if ( QgsWkbTypes::isCurvedType( wkbType() ) )
  {
    segmentized = QgsGeometry( static_cast< QgsCurve * >( d->geometry.get() )->segmentize() );
  }

  QgsVertexId previous;
  QgsVertexId next;
  if ( !QgsGeometryUtils::verticesAtDistance( *segmentized.geometry(), distance, previous, next ) )
    return 0.0;

  if ( previous == next )
  {
    // distance coincided exactly with a vertex
    QgsVertexId v2 = previous;
    QgsVertexId v1;
    QgsVertexId v3;
    QgsGeometryUtils::adjacentVertices( *segmentized.geometry(), v2, v1, v3 );
    if ( v1.isValid() && v3.isValid() )
    {
      QgsPoint p1 = segmentized.geometry()->vertexAt( v1 );
      QgsPoint p2 = segmentized.geometry()->vertexAt( v2 );
      QgsPoint p3 = segmentized.geometry()->vertexAt( v3 );
      double angle1 = QgsGeometryUtils::lineAngle( p1.x(), p1.y(), p2.x(), p2.y() );
      double angle2 = QgsGeometryUtils::lineAngle( p2.x(), p2.y(), p3.x(), p3.y() );
      return QgsGeometryUtils::averageAngle( angle1, angle2 );
    }
    else if ( v3.isValid() )
    {
      QgsPoint p1 = segmentized.geometry()->vertexAt( v2 );
      QgsPoint p2 = segmentized.geometry()->vertexAt( v3 );
      return QgsGeometryUtils::lineAngle( p1.x(), p1.y(), p2.x(), p2.y() );
    }
    else
    {
      QgsPoint p1 = segmentized.geometry()->vertexAt( v1 );
      QgsPoint p2 = segmentized.geometry()->vertexAt( v2 );
      return QgsGeometryUtils::lineAngle( p1.x(), p1.y(), p2.x(), p2.y() );
    }
  }
  else
  {
    QgsPoint p1 = segmentized.geometry()->vertexAt( previous );
    QgsPoint p2 = segmentized.geometry()->vertexAt( next );
    return QgsGeometryUtils::lineAngle( p1.x(), p1.y(), p2.x(), p2.y() );
  }
}

QgsGeometry QgsGeometry::intersection( const QgsGeometry &geometry ) const
{
  if ( !d->geometry || geometry.isNull() )
  {
    return QgsGeometry();
  }

  QgsGeos geos( d->geometry.get() );

  mLastError.clear();
  std::unique_ptr< QgsAbstractGeometry > resultGeom( geos.intersection( geometry.d->geometry.get(), &mLastError ) );

  if ( !resultGeom )
  {
    QgsGeometry geom;
    geom.mLastError = mLastError;
    return geom;
  }

  return QgsGeometry( std::move( resultGeom ) );
}

QgsGeometry QgsGeometry::combine( const QgsGeometry &geometry ) const
{
  if ( !d->geometry || geometry.isNull() )
  {
    return QgsGeometry();
  }

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  std::unique_ptr< QgsAbstractGeometry > resultGeom( geos.combine( geometry.d->geometry.get(), &mLastError ) );
  if ( !resultGeom )
  {
    QgsGeometry geom;
    geom.mLastError = mLastError;
    return geom;
  }
  return QgsGeometry( std::move( resultGeom ) );
}

QgsGeometry QgsGeometry::mergeLines() const
{
  if ( !d->geometry )
  {
    return QgsGeometry();
  }

  if ( QgsWkbTypes::flatType( d->geometry->wkbType() ) == QgsWkbTypes::LineString )
  {
    // special case - a single linestring was passed
    return QgsGeometry( *this );
  }

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  QgsGeometry result = geos.mergeLines( &mLastError );
  result.mLastError = mLastError;
  return result;
}

QgsGeometry QgsGeometry::difference( const QgsGeometry &geometry ) const
{
  if ( !d->geometry || geometry.isNull() )
  {
    return QgsGeometry();
  }

  QgsGeos geos( d->geometry.get() );

  mLastError.clear();
  std::unique_ptr< QgsAbstractGeometry > resultGeom( geos.difference( geometry.d->geometry.get(), &mLastError ) );
  if ( !resultGeom )
  {
    QgsGeometry geom;
    geom.mLastError = mLastError;
    return geom;
  }
  return QgsGeometry( std::move( resultGeom ) );
}

QgsGeometry QgsGeometry::symDifference( const QgsGeometry &geometry ) const
{
  if ( !d->geometry || geometry.isNull() )
  {
    return QgsGeometry();
  }

  QgsGeos geos( d->geometry.get() );

  mLastError.clear();
  std::unique_ptr< QgsAbstractGeometry > resultGeom( geos.symDifference( geometry.d->geometry.get(), &mLastError ) );
  if ( !resultGeom )
  {
    QgsGeometry geom;
    geom.mLastError = mLastError;
    return geom;
  }
  return QgsGeometry( std::move( resultGeom ) );
}

QgsGeometry QgsGeometry::extrude( double x, double y )
{
  QgsInternalGeometryEngine engine( *this );

  return engine.extrude( x, y );
}

QByteArray QgsGeometry::exportToWkb() const
{
  return d->geometry ? d->geometry->asWkb() : QByteArray();
}

QList<QgsGeometry> QgsGeometry::asGeometryCollection() const
{
  QList<QgsGeometry> geometryList;
  if ( !d->geometry )
  {
    return geometryList;
  }

  QgsGeometryCollection *gc = qgsgeometry_cast<QgsGeometryCollection *>( d->geometry.get() );
  if ( gc )
  {
    int numGeom = gc->numGeometries();
    geometryList.reserve( numGeom );
    for ( int i = 0; i < numGeom; ++i )
    {
      geometryList.append( QgsGeometry( gc->geometryN( i )->clone() ) );
    }
  }
  else //a singlepart geometry
  {
    geometryList.append( QgsGeometry( d->geometry->clone() ) );
  }

  return geometryList;
}

QPointF QgsGeometry::asQPointF() const
{
  QgsPointXY point = asPoint();
  return point.toQPointF();
}

QPolygonF QgsGeometry::asQPolygonF() const
{
  QPolygonF result;
  QgsPolyline polyline;
  QgsWkbTypes::Type type = wkbType();
  if ( type == QgsWkbTypes::LineString || type == QgsWkbTypes::LineString25D )
  {
    polyline = asPolyline();
  }
  else if ( type == QgsWkbTypes::Polygon || type == QgsWkbTypes::Polygon25D )
  {
    QgsPolygon polygon = asPolygon();
    if ( polygon.empty() )
      return result;
    polyline = polygon.at( 0 );
  }
  else
  {
    return result;
  }

  for ( const QgsPointXY &p : qgis::as_const( polyline ) )
  {
    result << p.toQPointF();
  }
  return result;
}

bool QgsGeometry::deleteRing( int ringNum, int partNum )
{
  if ( !d->geometry )
  {
    return false;
  }

  detach();
  bool ok = QgsGeometryEditUtils::deleteRing( d->geometry.get(), ringNum, partNum );
  return ok;
}

bool QgsGeometry::deletePart( int partNum )
{
  if ( !d->geometry )
  {
    return false;
  }

  if ( !isMultipart() && partNum < 1 )
  {
    setGeometry( nullptr );
    return true;
  }

  detach();
  bool ok = QgsGeometryEditUtils::deletePart( d->geometry.get(), partNum );
  return ok;
}

int QgsGeometry::avoidIntersections( const QList<QgsVectorLayer *> &avoidIntersectionsLayers, const QHash<QgsVectorLayer *, QSet<QgsFeatureId> > &ignoreFeatures )
{
  if ( !d->geometry )
  {
    return 1;
  }

  std::unique_ptr< QgsAbstractGeometry > diffGeom = QgsGeometryEditUtils::avoidIntersections( *( d->geometry ), avoidIntersectionsLayers, ignoreFeatures );
  if ( diffGeom )
  {
    reset( std::move( diffGeom ) );
  }
  return 0;
}


QgsGeometry QgsGeometry::makeValid() const
{
  if ( !d->geometry )
    return QgsGeometry();

  mLastError.clear();
  std::unique_ptr< QgsAbstractGeometry > g( _qgis_lwgeom_make_valid( d->geometry.get(), mLastError ) );

  QgsGeometry result = QgsGeometry( std::move( g ) );
  result.mLastError = mLastError;
  return result;
}


void QgsGeometry::validateGeometry( QList<QgsGeometry::Error> &errors, ValidationMethod method ) const
{
  QgsGeometryValidator::validateGeometry( *this, errors, method );
}

bool QgsGeometry::isGeosValid() const
{
  if ( !d->geometry )
  {
    return false;
  }

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  return geos.isValid( &mLastError );
}

bool QgsGeometry::isSimple() const
{
  if ( !d->geometry )
    return false;

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  return geos.isSimple( &mLastError );
}

bool QgsGeometry::isGeosEqual( const QgsGeometry &g ) const
{
  if ( !d->geometry || !g.d->geometry )
  {
    return false;
  }

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  return geos.isEqual( g.d->geometry.get(), &mLastError );
}

QgsGeometry QgsGeometry::unaryUnion( const QList<QgsGeometry> &geometries )
{
  QgsGeos geos( nullptr );

  QString error;
  std::unique_ptr< QgsAbstractGeometry > geom( geos.combine( geometries, &error ) );
  QgsGeometry result( std::move( geom ) );
  result.mLastError = error;
  return result;
}

QgsGeometry QgsGeometry::polygonize( const QList<QgsGeometry> &geometryList )
{
  QgsGeos geos( nullptr );

  QList<const QgsAbstractGeometry *> geomV2List;
  for ( const QgsGeometry &g : geometryList )
  {
    if ( !( g.isNull() ) )
    {
      geomV2List.append( g.geometry() );
    }
  }

  QString error;
  QgsGeometry result = geos.polygonize( geomV2List, &error );
  result.mLastError = error;
  return result;
}

void QgsGeometry::convertToStraightSegment()
{
  if ( !d->geometry || !requiresConversionToStraightSegments() )
  {
    return;
  }

  std::unique_ptr< QgsAbstractGeometry > straightGeom( d->geometry->segmentize() );
  reset( std::move( straightGeom ) );
}

bool QgsGeometry::requiresConversionToStraightSegments() const
{
  if ( !d->geometry )
  {
    return false;
  }

  return d->geometry->hasCurvedSegments();
}

QgsGeometry::OperationResult QgsGeometry::transform( const QgsCoordinateTransform &ct )
{
  if ( !d->geometry )
  {
    return QgsGeometry::InvalidBaseGeometry;
  }

  detach();
  d->geometry->transform( ct );
  return QgsGeometry::Success;
}

QgsGeometry::OperationResult QgsGeometry::transform( const QTransform &ct )
{
  if ( !d->geometry )
  {
    return QgsGeometry::InvalidBaseGeometry;
  }

  detach();
  d->geometry->transform( ct );
  return QgsGeometry::Success;
}

void QgsGeometry::mapToPixel( const QgsMapToPixel &mtp )
{
  if ( d->geometry )
  {
    detach();
    d->geometry->transform( mtp.transform() );
  }
}

QgsGeometry QgsGeometry::clipped( const QgsRectangle &rectangle )
{
  if ( !d->geometry || rectangle.isNull() || rectangle.isEmpty() )
  {
    return QgsGeometry();
  }

  QgsGeos geos( d->geometry.get() );
  mLastError.clear();
  std::unique_ptr< QgsAbstractGeometry > resultGeom = geos.clip( rectangle, &mLastError );
  if ( !resultGeom )
  {
    QgsGeometry result;
    result.mLastError = mLastError;
    return result;
  }
  return QgsGeometry( std::move( resultGeom ) );
}

void QgsGeometry::draw( QPainter &p ) const
{
  if ( d->geometry )
  {
    d->geometry->draw( p );
  }
}

static bool vertexIndexInfo( const QgsAbstractGeometry *g, int vertexIndex, int &partIndex, int &ringIndex, int &vertex )
{
  if ( vertexIndex < 0 )
    return false;  // clearly something wrong

  if ( const QgsGeometryCollection *geomCollection = qgsgeometry_cast<const QgsGeometryCollection *>( g ) )
  {
    partIndex = 0;
    int offset = 0;
    for ( int i = 0; i < geomCollection->numGeometries(); ++i )
    {
      const QgsAbstractGeometry *part = geomCollection->geometryN( i );

      // count total number of vertices in the part
      int numPoints = 0;
      for ( int k = 0; k < part->ringCount(); ++k )
        numPoints += part->vertexCount( 0, k );

      if ( vertexIndex < numPoints )
      {
        int nothing;
        return vertexIndexInfo( part, vertexIndex, nothing, ringIndex, vertex ); // set ring_index + index
      }
      vertexIndex -= numPoints;
      offset += numPoints;
      partIndex++;
    }
  }
  else if ( const QgsCurvePolygon *curvePolygon = qgsgeometry_cast<const QgsCurvePolygon *>( g ) )
  {
    const QgsCurve *ring = curvePolygon->exteriorRing();
    if ( vertexIndex < ring->numPoints() )
    {
      partIndex = 0;
      ringIndex = 0;
      vertex = vertexIndex;
      return true;
    }
    vertexIndex -= ring->numPoints();
    ringIndex = 1;
    for ( int i = 0; i < curvePolygon->numInteriorRings(); ++i )
    {
      const QgsCurve *ring = curvePolygon->interiorRing( i );
      if ( vertexIndex < ring->numPoints() )
      {
        partIndex = 0;
        vertex = vertexIndex;
        return true;
      }
      vertexIndex -= ring->numPoints();
      ringIndex += 1;
    }
  }
  else if ( const QgsCurve *curve = qgsgeometry_cast<const QgsCurve *>( g ) )
  {
    if ( vertexIndex < curve->numPoints() )
    {
      partIndex = 0;
      ringIndex = 0;
      vertex = vertexIndex;
      return true;
    }
  }
  else if ( qgsgeometry_cast<const QgsPoint *>( g ) )
  {
    if ( vertexIndex == 0 )
    {
      partIndex = 0;
      ringIndex = 0;
      vertex = 0;
      return true;
    }
  }

  return false;
}

bool QgsGeometry::vertexIdFromVertexNr( int nr, QgsVertexId &id ) const
{
  if ( !d->geometry )
  {
    return false;
  }

  id.type = QgsVertexId::SegmentVertex;

  bool res = vertexIndexInfo( d->geometry.get(), nr, id.part, id.ring, id.vertex );
  if ( !res )
    return false;

  // now let's find out if it is a straight or circular segment
  const QgsAbstractGeometry *g = d->geometry.get();
  if ( const QgsGeometryCollection *geomCollection = qgsgeometry_cast<const QgsGeometryCollection *>( g ) )
  {
    g = geomCollection->geometryN( id.part );
  }

  if ( const QgsCurvePolygon *curvePolygon = qgsgeometry_cast<const QgsCurvePolygon *>( g ) )
  {
    g = id.ring == 0 ? curvePolygon->exteriorRing() : curvePolygon->interiorRing( id.ring - 1 );
  }

  if ( const QgsCurve *curve = qgsgeometry_cast<const QgsCurve *>( g ) )
  {
    QgsPoint p;
    res = curve->pointAt( id.vertex, p, id.type );
    if ( !res )
      return false;
  }

  return true;
}

int QgsGeometry::vertexNrFromVertexId( QgsVertexId id ) const
{
  if ( !d->geometry )
  {
    return -1;
  }

  QgsCoordinateSequence coords = d->geometry->coordinateSequence();

  int vertexCount = 0;
  for ( int part = 0; part < coords.size(); ++part )
  {
    const QgsRingSequence &featureCoords = coords.at( part );
    for ( int ring = 0; ring < featureCoords.size(); ++ring )
    {
      const QgsPointSequence &ringCoords = featureCoords.at( ring );
      for ( int vertex = 0; vertex < ringCoords.size(); ++vertex )
      {
        if ( vertex == id.vertex && ring == id.ring && part == id.part )
        {
          return vertexCount;
        }
        ++vertexCount;
      }
    }
  }
  return -1;
}

QString QgsGeometry::lastError() const
{
  return mLastError;
}

void QgsGeometry::convertPointList( const QList<QgsPointXY> &input, QgsPointSequence &output )
{
  output.clear();
  for ( const QgsPointXY &p : input )
  {
    output.append( QgsPoint( p ) );
  }
}

void QgsGeometry::convertPointList( const QgsPointSequence &input, QList<QgsPointXY> &output )
{
  output.clear();
  for ( const QgsPoint &p : input )
  {
    output.append( QgsPointXY( p.x(), p.y() ) );
  }
}

QgsGeometry::operator bool() const
{
  return d->geometry.get();
}

void QgsGeometry::convertToPolyline( const QgsPointSequence &input, QgsPolyline &output )
{
  output.clear();
  output.resize( input.size() );

  for ( int i = 0; i < input.size(); ++i )
  {
    const QgsPoint &pt = input.at( i );
    output[i].setX( pt.x() );
    output[i].setY( pt.y() );
  }
}

void QgsGeometry::convertPolygon( const QgsPolygonV2 &input, QgsPolygon &output )
{
  output.clear();
  QgsCoordinateSequence coords = input.coordinateSequence();
  if ( coords.empty() )
  {
    return;
  }
  const QgsRingSequence &rings = coords[0];
  output.resize( rings.size() );
  for ( int i = 0; i < rings.size(); ++i )
  {
    convertToPolyline( rings[i], output[i] );
  }
}

GEOSContextHandle_t QgsGeometry::getGEOSHandler()
{
  return QgsGeos::getGEOSHandler();
}

QgsGeometry QgsGeometry::fromQPointF( QPointF point )
{
  return QgsGeometry( qgis::make_unique< QgsPoint >( point.x(), point.y() ) );
}

QgsGeometry QgsGeometry::fromQPolygonF( const QPolygonF &polygon )
{
  if ( polygon.isClosed() )
  {
    return QgsGeometry::fromPolygon( createPolygonFromQPolygonF( polygon ) );
  }
  else
  {
    return QgsGeometry::fromPolyline( createPolylineFromQPolygonF( polygon ) );
  }
}

QgsPolygon QgsGeometry::createPolygonFromQPolygonF( const QPolygonF &polygon )
{
  QgsPolygon result;
  result << createPolylineFromQPolygonF( polygon );
  return result;
}

QgsPolyline QgsGeometry::createPolylineFromQPolygonF( const QPolygonF &polygon )
{
  QgsPolyline result;
  for ( const QPointF &p : polygon )
  {
    result.append( QgsPointXY( p ) );
  }
  return result;
}

bool QgsGeometry::compare( const QgsPolyline &p1, const QgsPolyline &p2, double epsilon )
{
  if ( p1.count() != p2.count() )
    return false;

  for ( int i = 0; i < p1.count(); ++i )
  {
    if ( !p1.at( i ).compare( p2.at( i ), epsilon ) )
      return false;
  }
  return true;
}

bool QgsGeometry::compare( const QgsPolygon &p1, const QgsPolygon &p2, double epsilon )
{
  if ( p1.count() != p2.count() )
    return false;

  for ( int i = 0; i < p1.count(); ++i )
  {
    if ( !QgsGeometry::compare( p1.at( i ), p2.at( i ), epsilon ) )
      return false;
  }
  return true;
}


bool QgsGeometry::compare( const QgsMultiPolygon &p1, const QgsMultiPolygon &p2, double epsilon )
{
  if ( p1.count() != p2.count() )
    return false;

  for ( int i = 0; i < p1.count(); ++i )
  {
    if ( !QgsGeometry::compare( p1.at( i ), p2.at( i ), epsilon ) )
      return false;
  }
  return true;
}

QgsGeometry QgsGeometry::smooth( const unsigned int iterations, const double offset, double minimumDistance, double maxAngle ) const
{
  if ( !d->geometry || d->geometry->isEmpty() )
    return QgsGeometry();

  QgsGeometry geom = *this;
  if ( QgsWkbTypes::isCurvedType( wkbType() ) )
    geom = QgsGeometry( d->geometry->segmentize() );

  switch ( QgsWkbTypes::flatType( geom.wkbType() ) )
  {
    case QgsWkbTypes::Point:
    case QgsWkbTypes::MultiPoint:
      //can't smooth a point based geometry
      return geom;

    case QgsWkbTypes::LineString:
    {
      QgsLineString *lineString = static_cast< QgsLineString * >( d->geometry.get() );
      return QgsGeometry( smoothLine( *lineString, iterations, offset, minimumDistance, maxAngle ) );
    }

    case QgsWkbTypes::MultiLineString:
    {
      QgsMultiLineString *multiLine = static_cast< QgsMultiLineString * >( d->geometry.get() );

      std::unique_ptr< QgsMultiLineString > resultMultiline = qgis::make_unique< QgsMultiLineString> ();
      for ( int i = 0; i < multiLine->numGeometries(); ++i )
      {
        resultMultiline->addGeometry( smoothLine( *( static_cast< QgsLineString * >( multiLine->geometryN( i ) ) ), iterations, offset, minimumDistance, maxAngle ).release() );
      }
      return QgsGeometry( std::move( resultMultiline ) );
    }

    case QgsWkbTypes::Polygon:
    {
      QgsPolygonV2 *poly = static_cast< QgsPolygonV2 * >( d->geometry.get() );
      return QgsGeometry( smoothPolygon( *poly, iterations, offset, minimumDistance, maxAngle ) );
    }

    case QgsWkbTypes::MultiPolygon:
    {
      QgsMultiPolygonV2 *multiPoly = static_cast< QgsMultiPolygonV2 * >( d->geometry.get() );

      std::unique_ptr< QgsMultiPolygonV2 > resultMultiPoly = qgis::make_unique< QgsMultiPolygonV2 >();
      for ( int i = 0; i < multiPoly->numGeometries(); ++i )
      {
        resultMultiPoly->addGeometry( smoothPolygon( *( static_cast< QgsPolygonV2 * >( multiPoly->geometryN( i ) ) ), iterations, offset, minimumDistance, maxAngle ).release() );
      }
      return QgsGeometry( std::move( resultMultiPoly ) );
    }

    case QgsWkbTypes::Unknown:
    default:
      return QgsGeometry( *this );
  }
}

inline QgsPoint interpolatePointOnLine( const QgsPoint &p1, const QgsPoint &p2, const double offset )
{
  double deltaX = p2.x() - p1.x();
  double deltaY = p2.y() - p1.y();
  return QgsPoint( p1.x() + deltaX * offset, p1.y() + deltaY * offset );
}

std::unique_ptr< QgsLineString > smoothCurve( const QgsLineString &line, const unsigned int iterations,
    const double offset, double squareDistThreshold, double maxAngleRads,
    bool isRing )
{
  std::unique_ptr< QgsLineString > result = qgis::make_unique< QgsLineString >( line );
  for ( unsigned int iteration = 0; iteration < iterations; ++iteration )
  {
    QgsPointSequence outputLine;
    outputLine.reserve( 2 * ( result->numPoints() - 1 ) );
    bool skipFirst = false;
    bool skipLast = false;
    if ( isRing )
    {
      QgsPoint p1 = result->pointN( result->numPoints() - 2 );
      QgsPoint p2 = result->pointN( 0 );
      QgsPoint p3 = result->pointN( 1 );
      double angle = QgsGeometryUtils::angleBetweenThreePoints( p1.x(), p1.y(), p2.x(), p2.y(),
                     p3.x(), p3.y() );
      angle = std::fabs( M_PI - angle );
      skipFirst = angle > maxAngleRads;
    }
    for ( int i = 0; i < result->numPoints() - 1; i++ )
    {
      QgsPoint p1 = result->pointN( i );
      QgsPoint p2 = result->pointN( i + 1 );

      double angle = M_PI;
      if ( i == 0 && isRing )
      {
        QgsPoint p3 = result->pointN( result->numPoints() - 2 );
        angle = QgsGeometryUtils::angleBetweenThreePoints( p1.x(), p1.y(), p2.x(), p2.y(),
                p3.x(), p3.y() );
      }
      else if ( i < result->numPoints() - 2 )
      {
        QgsPoint p3 = result->pointN( i + 2 );
        angle = QgsGeometryUtils::angleBetweenThreePoints( p1.x(), p1.y(), p2.x(), p2.y(),
                p3.x(), p3.y() );
      }
      else if ( i == result->numPoints() - 2 && isRing )
      {
        QgsPoint p3 = result->pointN( 1 );
        angle = QgsGeometryUtils::angleBetweenThreePoints( p1.x(), p1.y(), p2.x(), p2.y(),
                p3.x(), p3.y() );
      }

      skipLast = angle < M_PI - maxAngleRads || angle > M_PI + maxAngleRads;

      // don't apply distance threshold to first or last segment
      if ( i == 0 || i >= result->numPoints() - 2
           || QgsGeometryUtils::sqrDistance2D( p1, p2 ) > squareDistThreshold )
      {
        if ( !isRing )
        {
          if ( !skipFirst )
            outputLine << ( i == 0 ? result->pointN( i ) : interpolatePointOnLine( p1, p2, offset ) );
          if ( !skipLast )
            outputLine << ( i == result->numPoints() - 2 ? result->pointN( i + 1 ) : interpolatePointOnLine( p1, p2, 1.0 - offset ) );
          else
            outputLine << p2;
        }
        else
        {
          // ring
          if ( !skipFirst )
            outputLine << interpolatePointOnLine( p1, p2, offset );
          else if ( i == 0 )
            outputLine << p1;
          if ( !skipLast )
            outputLine << interpolatePointOnLine( p1, p2, 1.0 - offset );
          else
            outputLine << p2;
        }
      }
      skipFirst = skipLast;
    }

    if ( isRing && outputLine.at( 0 ) != outputLine.at( outputLine.count() - 1 ) )
      outputLine << outputLine.at( 0 );

    result->setPoints( outputLine );
  }
  return result;
}

std::unique_ptr<QgsLineString> QgsGeometry::smoothLine( const QgsLineString &line, const unsigned int iterations, const double offset, double minimumDistance, double maxAngle ) const
{
  double maxAngleRads = maxAngle * M_PI / 180.0;
  double squareDistThreshold = minimumDistance > 0 ? minimumDistance * minimumDistance : -1;
  return smoothCurve( line, iterations, offset, squareDistThreshold, maxAngleRads, false );
}

std::unique_ptr<QgsPolygonV2> QgsGeometry::smoothPolygon( const QgsPolygonV2 &polygon, const unsigned int iterations, const double offset, double minimumDistance, double maxAngle ) const
{
  double maxAngleRads = maxAngle * M_PI / 180.0;
  double squareDistThreshold = minimumDistance > 0 ? minimumDistance * minimumDistance : -1;
  std::unique_ptr< QgsPolygonV2 > resultPoly = qgis::make_unique< QgsPolygonV2 >();

  resultPoly->setExteriorRing( smoothCurve( *( static_cast< const QgsLineString *>( polygon.exteriorRing() ) ), iterations, offset,
                               squareDistThreshold, maxAngleRads, true ).release() );

  for ( int i = 0; i < polygon.numInteriorRings(); ++i )
  {
    resultPoly->addInteriorRing( smoothCurve( *( static_cast< const QgsLineString *>( polygon.interiorRing( i ) ) ), iterations, offset,
                                 squareDistThreshold, maxAngleRads, true ).release() );
  }
  return resultPoly;
}

QgsGeometry QgsGeometry::convertToPoint( bool destMultipart ) const
{
  switch ( type() )
  {
    case QgsWkbTypes::PointGeometry:
    {
      bool srcIsMultipart = isMultipart();

      if ( ( destMultipart && srcIsMultipart ) ||
           ( !destMultipart && !srcIsMultipart ) )
      {
        // return a copy of the same geom
        return QgsGeometry( *this );
      }
      if ( destMultipart )
      {
        // layer is multipart => make a multipoint with a single point
        return fromMultiPoint( QgsMultiPoint() << asPoint() );
      }
      else
      {
        // destination is singlepart => make a single part if possible
        QgsMultiPoint multiPoint = asMultiPoint();
        if ( multiPoint.count() == 1 )
        {
          return fromPoint( multiPoint[0] );
        }
      }
      return QgsGeometry();
    }

    case QgsWkbTypes::LineGeometry:
    {
      // only possible if destination is multipart
      if ( !destMultipart )
        return QgsGeometry();

      // input geometry is multipart
      if ( isMultipart() )
      {
        const QgsMultiPolyline multiLine = asMultiPolyline();
        QgsMultiPoint multiPoint;
        for ( const QgsPolyline &l : multiLine )
          for ( const QgsPointXY &p : l )
            multiPoint << p;
        return fromMultiPoint( multiPoint );
      }
      // input geometry is not multipart: copy directly the line into a multipoint
      else
      {
        QgsPolyline line = asPolyline();
        if ( !line.isEmpty() )
          return fromMultiPoint( line );
      }
      return QgsGeometry();
    }

    case QgsWkbTypes::PolygonGeometry:
    {
      // can only transform if destination is multipoint
      if ( !destMultipart )
        return QgsGeometry();

      // input geometry is multipart: make a multipoint from multipolygon
      if ( isMultipart() )
      {
        const QgsMultiPolygon multiPolygon = asMultiPolygon();
        QgsMultiPoint multiPoint;
        for ( const QgsPolygon &poly : multiPolygon )
          for ( const QgsPolyline &line : poly )
            for ( const QgsPointXY &pt : line )
              multiPoint << pt;
        return fromMultiPoint( multiPoint );
      }
      // input geometry is not multipart: make a multipoint from polygon
      else
      {
        const QgsPolygon polygon = asPolygon();
        QgsMultiPoint multiPoint;
        for ( const QgsPolyline &line : polygon )
          for ( const QgsPointXY &pt : line )
            multiPoint << pt;
        return fromMultiPoint( multiPoint );
      }
    }

    default:
      return QgsGeometry();
  }
}

QgsGeometry QgsGeometry::convertToLine( bool destMultipart ) const
{
  switch ( type() )
  {
    case QgsWkbTypes::PointGeometry:
    {
      if ( !isMultipart() )
        return QgsGeometry();

      QgsMultiPoint multiPoint = asMultiPoint();
      if ( multiPoint.count() < 2 )
        return QgsGeometry();

      if ( destMultipart )
        return fromMultiPolyline( QgsMultiPolyline() << multiPoint );
      else
        return fromPolyline( multiPoint );
    }

    case QgsWkbTypes::LineGeometry:
    {
      bool srcIsMultipart = isMultipart();

      if ( ( destMultipart && srcIsMultipart ) ||
           ( !destMultipart && ! srcIsMultipart ) )
      {
        // return a copy of the same geom
        return QgsGeometry( *this );
      }
      if ( destMultipart )
      {
        // destination is multipart => makes a multipoint with a single line
        QgsPolyline line = asPolyline();
        if ( !line.isEmpty() )
          return fromMultiPolyline( QgsMultiPolyline() << line );
      }
      else
      {
        // destination is singlepart => make a single part if possible
        QgsMultiPolyline multiLine = asMultiPolyline();
        if ( multiLine.count() == 1 )
          return fromPolyline( multiLine[0] );
      }
      return QgsGeometry();
    }

    case QgsWkbTypes::PolygonGeometry:
    {
      // input geometry is multipolygon
      if ( isMultipart() )
      {
        const QgsMultiPolygon multiPolygon = asMultiPolygon();
        QgsMultiPolyline multiLine;
        for ( const QgsPolygon &poly : multiPolygon )
          for ( const QgsPolyline &line : poly )
            multiLine << line;

        if ( destMultipart )
        {
          // destination is multipart
          return fromMultiPolyline( multiLine );
        }
        else if ( multiLine.count() == 1 )
        {
          // destination is singlepart => make a single part if possible
          return fromPolyline( multiLine[0] );
        }
      }
      // input geometry is single polygon
      else
      {
        QgsPolygon polygon = asPolygon();
        // if polygon has rings
        if ( polygon.count() > 1 )
        {
          // cannot fit a polygon with rings in a single line layer
          // TODO: would it be better to remove rings?
          if ( destMultipart )
          {
            const QgsPolygon polygon = asPolygon();
            QgsMultiPolyline multiLine;
            for ( const QgsPolyline &line : polygon )
              multiLine << line;
            return fromMultiPolyline( multiLine );
          }
        }
        // no rings
        else if ( polygon.count() == 1 )
        {
          if ( destMultipart )
          {
            return fromMultiPolyline( polygon );
          }
          else
          {
            return fromPolyline( polygon[0] );
          }
        }
      }
      return QgsGeometry();
    }

    default:
      return QgsGeometry();
  }
}

QgsGeometry QgsGeometry::convertToPolygon( bool destMultipart ) const
{
  switch ( type() )
  {
    case QgsWkbTypes::PointGeometry:
    {
      if ( !isMultipart() )
        return QgsGeometry();

      QgsMultiPoint multiPoint = asMultiPoint();
      if ( multiPoint.count() < 3 )
        return QgsGeometry();

      if ( multiPoint.last() != multiPoint.first() )
        multiPoint << multiPoint.first();

      QgsPolygon polygon = QgsPolygon() << multiPoint;
      if ( destMultipart )
        return fromMultiPolygon( QgsMultiPolygon() << polygon );
      else
        return fromPolygon( polygon );
    }

    case QgsWkbTypes::LineGeometry:
    {
      // input geometry is multiline
      if ( isMultipart() )
      {
        QgsMultiPolyline multiLine = asMultiPolyline();
        QgsMultiPolygon multiPolygon;
        for ( QgsMultiPolyline::iterator multiLineIt = multiLine.begin(); multiLineIt != multiLine.end(); ++multiLineIt )
        {
          // do not create polygon for a 1 segment line
          if ( ( *multiLineIt ).count() < 3 )
            return QgsGeometry();
          if ( ( *multiLineIt ).count() == 3 && ( *multiLineIt ).first() == ( *multiLineIt ).last() )
            return QgsGeometry();

          // add closing node
          if ( ( *multiLineIt ).first() != ( *multiLineIt ).last() )
            *multiLineIt << ( *multiLineIt ).first();
          multiPolygon << ( QgsPolygon() << *multiLineIt );
        }
        // check that polygons were inserted
        if ( !multiPolygon.isEmpty() )
        {
          if ( destMultipart )
          {
            return fromMultiPolygon( multiPolygon );
          }
          else if ( multiPolygon.count() == 1 )
          {
            // destination is singlepart => make a single part if possible
            return fromPolygon( multiPolygon[0] );
          }
        }
      }
      // input geometry is single line
      else
      {
        QgsPolyline line = asPolyline();

        // do not create polygon for a 1 segment line
        if ( line.count() < 3 )
          return QgsGeometry();
        if ( line.count() == 3 && line.first() == line.last() )
          return QgsGeometry();

        // add closing node
        if ( line.first() != line.last() )
          line << line.first();

        // destination is multipart
        if ( destMultipart )
        {
          return fromMultiPolygon( QgsMultiPolygon() << ( QgsPolygon() << line ) );
        }
        else
        {
          return fromPolygon( QgsPolygon() << line );
        }
      }
      return QgsGeometry();
    }

    case QgsWkbTypes::PolygonGeometry:
    {
      bool srcIsMultipart = isMultipart();

      if ( ( destMultipart && srcIsMultipart ) ||
           ( !destMultipart && ! srcIsMultipart ) )
      {
        // return a copy of the same geom
        return QgsGeometry( *this );
      }
      if ( destMultipart )
      {
        // destination is multipart => makes a multipoint with a single polygon
        QgsPolygon polygon = asPolygon();
        if ( !polygon.isEmpty() )
          return fromMultiPolygon( QgsMultiPolygon() << polygon );
      }
      else
      {
        QgsMultiPolygon multiPolygon = asMultiPolygon();
        if ( multiPolygon.count() == 1 )
        {
          // destination is singlepart => make a single part if possible
          return fromPolygon( multiPolygon[0] );
        }
      }
      return QgsGeometry();
    }

    default:
      return QgsGeometry();
  }
}

QgsGeometryEngine *QgsGeometry::createGeometryEngine( const QgsAbstractGeometry *geometry )
{
  return new QgsGeos( geometry );
}

QDataStream &operator<<( QDataStream &out, const QgsGeometry &geometry )
{
  out << geometry.exportToWkb();
  return out;
}

QDataStream &operator>>( QDataStream &in, QgsGeometry &geometry )
{
  QByteArray byteArray;
  in >> byteArray;
  if ( byteArray.isEmpty() )
  {
    geometry.setGeometry( nullptr );
    return in;
  }

  geometry.fromWkb( byteArray );
  return in;
}
