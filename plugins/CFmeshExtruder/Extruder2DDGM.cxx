// Copyright (C) 2012 von Karman Institute for Fluid Dynamics, Belgium
//
// This software is distributed under the terms of the
// GNU Lesser General Public License version 3 (LGPLv3).
// See doc/lgpl.txt and doc/gpl.txt for the license text.

#include <ctime>
#include <cstdlib>

#include "Common/Stopwatch.hh"
#include "Common/BadValueException.hh"

#include "Environment/DirPaths.hh"
#include "Environment/ObjectProvider.hh"

// #include "CFmeshFormatChecker/CFmeshFileChecker.hh" // move the checker to Framework
#include "CFmeshExtruder/Extruder2DDGM.hh"
#include "CFmeshExtruder/CFmeshExtruder.hh"

///////////////////////////////////////////()///////////////////////////////////

using namespace std;
using namespace COOLFluiD::Framework;
using namespace COOLFluiD::Common;

//////////////////////////////////////////////////////////////////////////////

namespace COOLFluiD {

  namespace IO {

    namespace CFmeshExtruder {

      const CFuint nodesInTetra = 4;
      const CFuint nodesInPrism = 6;
      const CFuint nodesInHexa  = 8;

//////////////////////////////////////////////////////////////////////////////

Environment::ObjectProvider<Extruder2DDGM,
                            MeshFormatConverter,
                            CFmeshExtruderModule,
                            1>
Extruder2DDGMProvider("Extruder2DDGM");

//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::defineConfigOptions(Config::OptionList& options)
{
   options.addConfigOption< bool >("Split","Split into tetrahedra.");
   options.addConfigOption< bool >("Random","Random positions of nodes in inner layers.");
   options.addConfigOption< CFreal >("ExtrudeSize","Extrude size in z coordinate.");
   options.addConfigOption< CFuint >("NbLayers","Nb of Layers to extrude from the 2D mesh.");
}

//////////////////////////////////////////////////////////////////////////////

Extruder2DDGM::Extruder2DDGM(const std::string& name)
  : MeshFormatConverter(name),
    _data(new CFmeshReaderWriterSource()),
    _newTetras(3),
    _newTriag(2)
{
  addConfigOptionsTo(this);

  SafePtr<CFmeshReaderWriterSource> ptr = _data.get();
  _reader.setReadData(ptr);
  _writer.setWriteData(ptr);

  // configuration options

  _nbLayers = 1;
  setParameter("NbLayers",&_nbLayers);

  _zSize = 1.0;
  setParameter("ExtrudeSize",&_zSize);

  _zDelta = _zSize / _nbLayers;

  _split = false;
  setParameter("Split",&_split);

  _random = false;
  setParameter("Random",&_random);
}

//////////////////////////////////////////////////////////////////////////////

Extruder2DDGM::~Extruder2DDGM()
{
}

//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::configure ( Config::ConfigArgs& args )
{
  ConfigObject::configure(args);

//   if (_split == true)
//     throw Common::NotImplementedException("Extruder2DDGM hasn't an implementation to split the extruded 3D meshes into tetrahedra.");

  cf_assert(_nbLayers > 0);
  if (!(std::abs(_zSize) > 0.0))
    throw BadValueException(FromHere(),"Extruder2DDGM received zero extruding size");

  _zDelta = _zSize / _nbLayers;


  if (_split != false){
    //CFout << _newTetras.size() << "\n";
    for (CFuint i=0;i < _newTetras.size();++i){
      _newTetras[i].resize(4);
      //CFout << _newTetras[i].size() << "\n";
      }
    for (CFuint i=0;i < _newTriag.size();++i){
      _newTriag[i].resize(3);
      //CFout << _newTetras[i].size() << "\n";
      }
  }
}

//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::checkFormat(const boost::filesystem::path& filepath)
{
//  CFmeshFormatChecker::CFmeshFileChecker checker("checker");
//  checker.check(boost::filesystem::change_extension(filepath,getOriginExtension()));

/// @todo CFmeshFormatChecker should be moved to framework
}

//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::convert(const boost::filesystem::path& fromFilepath,
       const boost::filesystem::path& filepath)
{
  CFAUTOTRACE;

  Common::Stopwatch<WallTime> stp;
  stp.start();

  // reads the origin 2D CFmesh

  _reader.readFromFile(fromFilepath);

  // transforms the data by extruding in z coordinate

  extrude();

  // write the new 3D data to the file

  _writer.writeToFile(filepath);

  stp.stop();
  CFout << "Extrusion from 2D CFmesh to 3D CFmesh took: " << stp.read() << "s\n";
}

//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::convertBack(const boost::filesystem::path& filepath)
{
  CFLog(VERBOSE,"Makes no sense trying to convert back from a extruded 3D CFmesh" << "\n");
}

//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::extrude()
{
  CFAUTOTRACE;

  CFmeshReaderWriterSource& data = *(_data.get());

  cf_assert(data.getGeometricPolyOrder() == CFPolyOrder::ORDER1);

  ///This is for FVM!!!
  cf_assert(data.getSolutionPolyOrder()  == CFPolyOrder::ORDER1);

  data.consistencyCheck();

  // set dimension to 3D
  cf_assert(data.getDimension() == DIM_2D);
  data.setDimension(DIM_3D);

  // save connectivities previous to extrusion
  data.copyElementNodeTo(_oldElemNode);
  data.copyElementStateTo(_oldElemState);

  transformNodesTo3D();

  createFirstLayer();

  // create subsequent layers of elements
  if (_nbLayers > 1){
  for(CFuint iLayer = 1; iLayer < _nbLayers; ++iLayer) {
    _iLayer = iLayer;
    createAnotherLayer();
  }
  if (_random == true) randomNodes();
  }

  //if(isHybrid) reorderElementNodeState();

  updateTRSData();

  SafePtr< vector<RealVector> > nodes  = data.getNodeList();
  SafePtr< vector<RealVector> > states = data.getStateList();

  data.setNbUpdatableNodes(nodes->size());
  data.setNbNonUpdatableNodes(0);

  data.setNbUpdatableStates(states->size());
  data.setNbNonUpdatableStates(0);

  data.consistencyCheck();
}

//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::transformNodesTo3D()
{
  CFAUTOTRACE;

  CFmeshReaderWriterSource& data = *(_data.get());

// @todo check THIS
  RealVector newValue(DIM_3D);
  const CFuint nbNodes = data.getTotalNbNodes();

  for(CFuint iNode = 0; iNode < nbNodes; ++iNode) {
    const RealVector* oldValue = data.getNode(iNode);
    for (CFuint i = 0; i < DIM_2D; ++i) {
      newValue[i] = (*oldValue)[i];
    }
    newValue[DIM_2D] = 0.0;

    data.resizeOneNode(iNode);
    data.setNode(iNode, newValue);
  }
}

//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::createFirstLayer()
{
  CFAUTOTRACE;

  migrateElementTypes();

  convertCurrentElementsTo3D();
}

//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::convertCurrentElementsTo3D()
{
  CFAUTOTRACE;

  CFmeshReaderWriterSource& data = *(_data.get());

  _nbNodesPerLayer   = data.getTotalNbNodes();
  _nbStatesPerLayer  = data.getTotalNbStates();
  const CFuint z = 2;

  /// this is for FVM
//   cf_assert(data.getNbElements() == data.getTotalNbStates());

  SafePtr< vector<RealVector> > nodes  = data.getNodeList();
  SafePtr< vector<RealVector> > states = data.getStateList();

  nodes->reserve (2 * nodes->size());

  RealVector tmpNode(DIM_3D);
  for(CFuint iNode = 0; iNode < data.getTotalNbNodes(); ++iNode) {

    tmpNode = (*nodes)[iNode];
    tmpNode[z] += _zDelta;

    nodes->push_back(tmpNode);
  }

  SafePtr< Table<CFuint> > elementNode  = data.getElementNode();
  SafePtr< Table<CFuint> > elementState = data.getElementState();

  elementNode->clear();
  elementState->clear();

  SafePtr< vector<ElementTypeData> > elementType =
    data.getElementTypeData();

  const CFuint nbElements = data.getNbElements();

  if (_split != true) {
    _nodePattern.resize(nbElements);
    _statePattern.resize(nbElements);
    }
  else
    {
    _nodePattern.resize(3*nbElements);
    _statePattern.resize(3*nbElements);
    }

  const CFuint nbElementTypes = data.getNbElementTypes();
  cf_assert(nbElementTypes == elementType->size());

  // search for the specific element type IDs
  CFuint elemID = 0;
  for(CFuint iType = 0; iType <  nbElementTypes; ++iType) {

    if((*elementType)[iType].getGeoShape() ==  CFGeoShape::TETRA) {
      _tetraTypeID = iType;
    }

    if((*elementType)[iType].getGeoShape() ==  CFGeoShape::PRISM) {
      _prismTypeID = iType;
    }

    if((*elementType)[iType].getGeoShape() ==  CFGeoShape::HEXA) {
      _hexaTypeID = iType;
    }

    const CFuint nbElemsPerType = (*elementType)[iType].getNbElems();
    const CFGeoShape::Type elemGeoShape = (*elementType)[iType].getGeoShape();
    for(CFuint iElem = 0; iElem < nbElemsPerType; ++iElem, ++elemID) {

      switch(elemGeoShape) {

      case CFGeoShape::TETRA:
          _nodePattern[elemID] = nodesInTetra;
          _nodePattern[elemID + nbElemsPerType] = nodesInTetra;
          _nodePattern[elemID + 2*nbElemsPerType] = nodesInTetra;
          _statePattern[elemID] = 4;
          _statePattern[elemID + nbElemsPerType] = 4;
          _statePattern[elemID + 2*nbElemsPerType] = 4;
        break;

      case CFGeoShape::PRISM:
        _nodePattern[elemID] = nodesInPrism;
        _statePattern[elemID] = 1;
        break;

      case CFGeoShape::HEXA:
        _nodePattern[elemID] = nodesInHexa;
        _statePattern[elemID] = 1;
        break;

      default:
        std::string shape =
          CFGeoShape::Convert::to_str(elemGeoShape);
        std::string msg = std::string("Wrong kind of elements present in 2D mesh: ") + shape;
        throw BadValueException (FromHere(),msg);
      }
    }
  }

  elementNode->resize(_nodePattern);
  elementState->resize(_statePattern);

  CFuint newNbElements = nbElements;
  _nbElemPerLayer = nbElements;

  elemID = 0;
  for (CFuint iType = 0; iType < nbElementTypes; ++iType) {
    const CFGeoShape::Type currShape = (*elementType)[iType].getGeoShape();
    const CFuint nbElemPerType = (*elementType)[iType].getNbElems();

    for(CFuint iElem = 0; iElem < nbElemPerType; ++iElem) {

      switch(currShape) {

      case CFGeoShape::TETRA:

        {
        newNbElements += 2;

        // here we assume all the elements are of the same type !!
        /// @todo change this

        vector<CFuint> newID(3);
        newID[0] = iElem;
        newID[1] = iElem + _nbElemPerLayer;
        newID[2] = iElem + 2*_nbElemPerLayer;

        CFuint oldNbElemsPerType = (*elementType)[_tetraTypeID].getNbElems();
        (*elementType)[_tetraTypeID].setNbElems(oldNbElemsPerType + 2);

        vector<CFuint> tempPrism;
        tempPrism.resize(6);
        // Create the Prism
        for(CFuint localID = 0; localID < 3; ++localID) {
            tempPrism[localID] = _oldElemNode(elemID,localID);
            tempPrism[localID+3] = _nbNodesPerLayer + _oldElemNode(elemID,localID);
            //CFout << tempPrism[localID] << "  " << tempPrism[localID+3] << "\n";
            }

        splitPrism(tempPrism);

        //Create the tetrahedras
        for (CFuint iTetra = 0; iTetra < 3; ++iTetra) {
          for(CFuint localID = 0; localID < 4; ++localID) {
            // first layer
            //CFout << "newID[iTetra]: " << newID[iTetra] << "\n";
            //CFout << _newTetras[iTetra][localID]  << " " << localID << "\n";
            (*elementNode)(newID[iTetra],localID) = _newTetras[iTetra][localID];
            (*elementState)(newID[iTetra],localID) = newID[iTetra]*4 + localID;
//             (*elementState)(newID[iTetra],localID) = _newTetras[iTetra][localID];

          }
        }

        ++elemID;
        }
        break;

      case CFGeoShape::PRISM:
        {
        for(CFuint localID = 0; localID < 3; ++localID) {
          // first layer
          (*elementNode)(elemID,localID) =
            _oldElemNode(elemID,localID);

          // next layer
          (*elementNode)(elemID,localID + 3) =
            _nbNodesPerLayer  + _oldElemNode (elemID,localID);
        }

        // state
        (*elementState)(elemID,0) = _oldElemState(elemID,0);

        ++elemID;
        }
        break;

      case CFGeoShape::HEXA:
        {
        for(CFuint localID = 0; localID < 4; ++localID) {
          // first layer
          (*elementNode)(elemID,localID) =
            _oldElemNode (elemID,localID);

          // next layer
          (*elementNode)(elemID,localID + 4) =
            _nbNodesPerLayer  + _oldElemNode (elemID,localID);
        }

        // state
        (*elementState)(elemID,0) = _oldElemState(elemID,0);

        ++elemID;
        }
        break;

      default:

        std::string shape = CFGeoShape::Convert::to_str(currShape);

        std::string msg = std::string("Wrong kind of elements present in 2D mesh: ") +
                       shape +
                       std::string(" ElemID: ") +
                       Common::StringOps::to_str(++elemID);
        throw BadValueException (FromHere(),msg);
        }
    }
  }

  cf_assert(elemID == nbElements);

  _nbElemPerLayer = nbElements;
  data.setNbElements(newNbElements);
}

//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::migrateElementTypes()
{
  CFAUTOTRACE;

  CFmeshReaderWriterSource& data = *(_data.get());

  SafePtr<vector<ElementTypeData> > elementType = data.getElementTypeData();

  const CFuint nbTypeShapes = elementType->size();
  // only accept TRIAG and QUAD present
  cf_assert(nbTypeShapes <= 2);

  for(CFuint iType = 0; iType < nbTypeShapes; ++iType) {

    if (_split == true){
      (*elementType)[iType].setGeoShape(CFGeoShape::TETRA);
      (*elementType)[iType].setShape( CFGeoShape::Convert::to_str(CFGeoShape::TETRA) );
      (*elementType)[iType].setNbNodes(nodesInTetra);
      (*elementType)[iType].setNbStates(4);
    }
    else{
      if((*elementType)[iType].getGeoShape() == CFGeoShape::TRIAG) {
        (*elementType)[iType].setGeoShape(CFGeoShape::PRISM);
        (*elementType)[iType].setShape( CFGeoShape::Convert::to_str(CFGeoShape::PRISM) );
        (*elementType)[iType].setNbNodes(nodesInPrism);
        (*elementType)[iType].setNbStates(1);
      }
      else if((*elementType)[iType].getGeoShape() == CFGeoShape::QUAD) {

        (*elementType)[iType].setGeoShape( CFGeoShape::HEXA );
        (*elementType)[iType].setShape(  CFGeoShape::Convert::to_str(CFGeoShape::HEXA) );
        (*elementType)[iType].setNbNodes(nodesInHexa);
        (*elementType)[iType].setNbStates(1);
      }
      else {
        std::string shape = CFGeoShape::Convert::to_str((*elementType)[iType].getGeoShape());
        std::string msg = std::string("Wrong kind of elements present in 2D mesh: ") + shape;
        throw BadValueException(FromHere(),msg);
      }
    }
  }
}

//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::createAnotherLayer()
{
  CFAUTOTRACE;

  CFmeshReaderWriterSource& data = *(_data.get());
  SafePtr< vector<RealVector> > nodes  = data.getNodeList();
  SafePtr< vector<RealVector> > states = data.getStateList();

  const CFuint z = 2;

  const CFuint nStartID = nodes->size() - _nbNodesPerLayer;
  const CFuint nEndID   = nodes->size();

  const CFuint sStartID = states->size() - _nbStatesPerLayer;
  const CFuint sEndID   = states->size();

  // Add new nodes and states for the new layer
  RealVector tmpNode(DIM_3D);
  for(CFuint iNode = nStartID; iNode < nEndID; ++iNode) {

    tmpNode = (*nodes)[iNode];
    tmpNode[z] += _zDelta;

    nodes->push_back(tmpNode);
  }

  for(CFuint iState = sStartID; iState < sEndID; ++iState) {
    states->push_back((*states)[iState]);
  }

  SafePtr< Table<CFuint> > elementNode  = data.getElementNode();
  SafePtr< Table<CFuint> > elementState  = data.getElementState();

//unused//  const CFuint oldNbElements = elementNode->nbRows();
  elementNode->increase(_nodePattern);
  elementState->increase(_statePattern);

  CFuint nbElements = data.getNbElements();
  CFuint eStartID = nbElements - _nbElemPerLayer;
  CFuint eEndID   = nbElements;
  if(_split){
    eStartID = 0;
    eEndID   = _nbElemPerLayer;
  }

  CFuint newNbElements = nbElements;

  ///@todo  later RECHECK THIS PART ACCURATELY
  SafePtr< vector<ElementTypeData> > elementType = data.getElementTypeData();

  // create a cashed list of the element shapes
  vector<CFGeoShape::Type> elementShapes(nbElements);
  const CFuint nbElementTypes = elementType->size();

  CFuint elemID = 0;
  for (CFuint iLayer = 0; iLayer < _iLayer; ++iLayer) {
    for (CFuint iType = 0; iType < nbElementTypes; ++iType) {
      const CFGeoShape::Type currElemType = (*elementType)[iType].getGeoShape();
      const CFuint nbElemPerType = (*elementType)[iType].getNbElems();
      const CFuint nbElemPerTypePerLayer = nbElemPerType/_iLayer;

      for (CFuint iElem = 0; iElem < nbElemPerTypePerLayer; ++iElem) {
        elementShapes[elemID] = currElemType;
        ++elemID;
      }
  }
  }

   cf_assert(nbElements == elemID);
// std::cout << "eStartID: " <<eStartID<<std::endl;
// std::cout << "eEndID: " <<eEndID<<std::endl;

  for(CFuint iElem = eStartID; iElem < eEndID; ++iElem) {

    ++newNbElements;

    CFuint newID = iElem + _nbElemPerLayer;

    CFuint oldNbElemsPerType = 0;

    // is this iElem valid???? check
    switch(elementShapes[iElem]) {

    // for tetrahedras, we assume that we start from triangles
    case CFGeoShape::TETRA:
        {
        newNbElements += 2;

        newID += (nbElements - _nbElemPerLayer);
        // here we assume all the elements are of the same type !!
        /// @todo change this
        vector<CFuint> newIDs(3);
        newIDs[0] = newID;
        newIDs[1] = newID + _nbElemPerLayer;
        newIDs[2] = newID + 2*_nbElemPerLayer;

        CFuint oldNbElemsPerType = (*elementType)[_tetraTypeID].getNbElems();
        (*elementType)[_tetraTypeID].setNbElems(oldNbElemsPerType + 3);

        vector<CFuint> tempPrism;
        tempPrism.resize(6);
        // Create the Prism
        for(CFuint localID = 0; localID < 3; ++localID) {
            tempPrism[localID] = (*elementNode)(iElem,localID) + _iLayer*_nbNodesPerLayer;
            tempPrism[localID+3] = (*elementNode)(iElem,localID) + (_iLayer+1)*_nbNodesPerLayer;
            }

        splitPrism(tempPrism);

        //Create the tetrahedras
        for (CFuint iTetra = 0; iTetra < 3; ++iTetra) {
          for(CFuint localID = 0; localID < 4; ++localID) {
            // first layer
// CFout << "newIDs[iTetra]: " << newIDs[iTetra] << "\n";
// CFout << _newTetras[iTetra][localID]  << " " << localID << "\n";
            (*elementNode)(newIDs[iTetra],localID) = _newTetras[iTetra][localID];
            (*elementState)(newIDs[iTetra],localID) = newIDs[iTetra]*4 + localID;
//             (*elementState)(newIDs[iTetra],localID) = _newTetras[iTetra][localID];

          }
        }
      }
      break;


    case CFGeoShape::PRISM:
      {
// std::cout << "Prism" <<std::endl;
// std::cout << "newID: " << newID << std::endl;
// std::cout << "elementStateSize: " << elementState->nbRows() << std::endl;
// std::cout << "elementNodeSize: " << elementNode->nbRows() << std::endl;
// std::cout << "elementNodeCols[" << newID <<"]: " << elementNode->nbCols(newID) << std::endl;

      oldNbElemsPerType = (*elementType)[_prismTypeID].getNbElems();
      (*elementType)[_prismTypeID].setNbElems(oldNbElemsPerType + 1);

      for(CFuint localID = 0; localID < 3; ++localID) {
// std::cout << "localID: " << localID << std::endl;
        // first layer
        (*elementNode)(newID,localID) =
          (*elementNode)(iElem,localID) + _nbNodesPerLayer;
// std::cout << "localID+3: " << localID+3 << std::endl;
        // next layer
        (*elementNode)(newID,localID + 3) =
          (*elementNode)(iElem,localID + 3) + _nbNodesPerLayer;
      }

// std::cout << "state " << std::endl;
      // state
      (*elementState)(newID,0) = (*elementState)(iElem,0) + _nbStatesPerLayer;

      }
      break;

    case CFGeoShape::HEXA:
      {
// std::cout << "Hexa" <<std::endl;
// std::cout << "newID: " << newID << std::endl;
// std::cout << "Node pattern.size(): " <<_nodePattern.size() <<std::endl;
// std::cout << "State pattern.size(): " <<_statePattern.size() <<std::endl;
// std::cout << "NodePatt: " <<_nodePattern[iElem] << std::endl;
// std::cout << "StatePatt: " <<_statePattern[iElem] << std::endl;


      oldNbElemsPerType = (*elementType)[_hexaTypeID].getNbElems();
      (*elementType)[_hexaTypeID].setNbElems(oldNbElemsPerType + 1);
// std::cout << "State pattern[iElem]: " <<_statePattern[iElem] <<std::endl;
// std::cout << "Node pattern[iElem]: " <<_nodePattern[iElem] <<std::endl;
// std::cout << "elementStateSize: " << elementState->nbRows() << std::endl;
// std::cout << "elementNodeSize: " << elementNode->nbRows() << std::endl;
// std::cout << "elementNodeCols[" << newID <<"]: " << elementNode->nbCols(newID) << std::endl;
// std::cout << "newID: " << newID << std::endl;
// std::cout << "iElem: " << iElem << std::endl;
// std::cout << "_nbStatesPerLayer: " << _nbStatesPerLayer << std::endl;
// std::cout << "_nbNodesPerLayer: " << _nbNodesPerLayer << std::endl;

      for(CFuint localID = 0; localID < 4; ++localID) {
// std::cout << "localID: " << localID << std::endl;

        // first layer
        (*elementNode)(newID,localID) =
          (*elementNode)(iElem,localID) + _nbNodesPerLayer;

// std::cout << "localID+4: " << localID+4 << std::endl;

        // next layer
        (*elementNode)(newID,localID + 4) =
          (*elementNode)(iElem,localID + 4) + _nbNodesPerLayer;
      }

      // state
      (*elementState)(newID,0) = (*elementState)(iElem,0) + _nbStatesPerLayer;
      //(*elementState)(newID,0) = _oldElemState(iElem,0) + _nbStatesPerLayer;

      }
      break;

    default:
      std::string shape = CFGeoShape::Convert::to_str ( elementShapes[iElem] );
      std::string msg = std::string("Unexpected type of element: ") + shape;
      throw BadValueException(FromHere(),msg);
    }
  }

  data.setNbElements(newNbElements);
}

//////////////////////////////////////////////////////////////////////////////

// void Extruder2DDGM::reorderElementNodeState()
// {
//
//   SafePtr< Table<CFuint> > elementNode  = data.getElementNode();
//   SafePtr< Table<CFuint> > elementState  = data.getElementState();
//
//   CFuint nbTetras = 0;
//   CFuint nbPrisms = 0;
//   CFuint nbHexas = 0;
//
//   //used for backup
//   Table<CFuint> tempElementConnTetra(nbTetras,4);
//   Table<CFuint> tempElementConnPrism(nbPrisms,6);
//   Table<CFuint> tempElementConnHexa(nbHexas,8);
// CFuint nbNodesInTetra = 4;
// CFuint nbNodesInPrism = 6;
// CFuint nbNodesInHexa = 8;
//
//   //reorder elementNode table
//   //fill the tetras
//   CFuint newID = 0;
//   for(CFuint iElem=0; iElem < nbElements;++iElem)
//   {
//     if( (*elementNode)[iElem].size() == nbNodesInTetra){
//       for(CFuint iNode=0; iNode < nbNodesInTetra;++iNode){
//         tempElementConnTetra(newID,iNode) = (*elementNode)(iElem,iNode);
//       }
//       ++newID;
//       }
//
//   }
//
//
// }

//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::updateTRSData()
{
  extrudeCurrentTRSs();
  createTopBottomTRSs();
}

//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::extrudeCurrentTRSs()
{
  CFAUTOTRACE;

  CFuint nbNodesInGeo = 4;
  CFuint nbStatesInGeo = 4;

  if (_split){
    nbNodesInGeo = 3;
    nbStatesInGeo = 3;
  }

  const CFuint halfNodesInGeo = 2;
//  const CFuint halfStatesInGeo = 2;

  CFmeshReaderWriterSource& data = *(_data.get());

  SafePtr< vector<CFuint> > nbTRs = data.getNbTRs();
  SafePtr< vector<vector<CFuint> > > nbGeomEntsPerTR = data.getNbGeomEntsPerTR();

  for(CFuint iTRS = 0; iTRS < data.getNbTRSs(); ++iTRS ) {

    TRGeoConn& geoConn = data.getTRGeoConn(iTRS);

    for(CFuint iTR = 0; iTR < (*nbTRs)[iTRS]; ++iTR ) {

      GeoConn& geoC = geoConn[iTR];
      const CFuint oldNbGeos = geoC.size();
      CFuint nbGeos = oldNbGeos;
      if (_split) nbGeos *= 2;

      // make a copy of the old connectivity
      GeoConn oldConn = geoC;

      // if
      if (_split){
        CFuint nbTriagsPerQuad = 2 ;
        geoC.resize(_nbLayers * oldNbGeos * nbTriagsPerQuad);
      }
      else{
        geoC.resize(_nbLayers * oldNbGeos);
      }

      for(CFuint iLayer = 0; iLayer < _nbLayers; ++iLayer) {

    for (CFuint iGeo = 0; iGeo < oldNbGeos; ++iGeo) {

    // Dirty trick to avoid out of range problems when not splitting
    CFuint temp = oldNbGeos;
    if (!_split) temp = 0;

    std::valarray<CFuint>& nodeCon  = geoC[iGeo + (iLayer * nbGeos)].first;
    std::valarray<CFuint>& stateCon = geoC[iGeo + (iLayer * nbGeos)].second;
    std::valarray<CFuint>& nodeCon2  = geoC[iGeo+temp + (iLayer * nbGeos)].first;
    std::valarray<CFuint>& stateCon2 = geoC[iGeo+temp + (iLayer * nbGeos)].second;

    std::valarray<CFuint>& oldNodeCon  = oldConn[iGeo].first;
    std::valarray<CFuint>& oldStateCon = oldConn[iGeo].second;

    nodeCon.resize(nbNodesInGeo);
    stateCon.resize(nbStatesInGeo);
    nodeCon2.resize(nbNodesInGeo);
    stateCon2.resize(nbStatesInGeo);

    // change node connectivity
    if (!_split){
    for(CFuint nodeID = 0; nodeID < halfNodesInGeo; ++nodeID) {
      nodeCon[nodeID] =
        oldNodeCon[nodeID] + iLayer * _nbNodesPerLayer;
      nodeCon[nodeID + halfNodesInGeo] =
        oldNodeCon[nodeID] + (iLayer + 1) * _nbNodesPerLayer;
    }

    // change state connectivity
    for(CFuint stateID = 0; stateID < 1; ++stateID) {
      stateCon[stateID] = oldStateCon[stateID] + iLayer * _nbStatesPerLayer;
    }
//    for(CFuint stateID = 0; stateID < halfStatesInGeo; ++stateID) {
/*      stateCon[stateID + halfStatesInGeo] =
        oldStateCon[stateID] + (iLayer + 1) * _nbStatesPerLayer;*/
//    }
    }
    else{
    //CFuint nbNodesInTriag = 3;
    //CFuint nbNodesInTetra = 4;

    // Compute the tetrahedras
    vector<CFuint> tempQuad(4);
    // Create the Quads
    for(CFuint nodeID = 0; nodeID < halfNodesInGeo; ++nodeID) {
      tempQuad[nodeID] = oldNodeCon[nodeID] + iLayer * _nbNodesPerLayer;
      tempQuad[nodeID+halfNodesInGeo] = oldNodeCon[nodeID] + (iLayer + 1) * _nbNodesPerLayer;
      }
    swap(tempQuad[2],tempQuad[3]);
    splitQuads(tempQuad);

    // Set the node connectivity
    for(CFuint stateID = 0; stateID < 3; ++stateID) {
      nodeCon[stateID] = _newTriag[0][stateID];
      nodeCon2[stateID] = _newTriag[1][stateID];
      }

    // Do the same for state connectivity
    // Create the Quads
    for(CFuint nodeID = 0; nodeID < halfNodesInGeo; ++nodeID) {
      tempQuad[nodeID] = oldStateCon[nodeID] + iLayer * _nbStatesPerLayer;
      tempQuad[nodeID+halfNodesInGeo] = oldStateCon[nodeID] + (iLayer + 1) * _nbStatesPerLayer;
      }
    swap(tempQuad[2],tempQuad[3]);
    splitQuads(tempQuad);

    // Set the state connectivity
    for(CFuint stateID = 0; stateID < 3; ++stateID) {
      stateCon[stateID] = _newTriag[0][stateID];
      stateCon2[stateID] = _newTriag[1][stateID];
      }

    }

    if (_split != true){
    swap(nodeCon[2],nodeCon[3]);
    //swap(stateCon[2],stateCon[3]);
    }
  }
      }

      (*nbGeomEntsPerTR)[iTRS][iTR] = geoC.size();
    }
  }
}

//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::createTopBottomTRSs()
{
  createSideTRS("Bottom",0,true);
  createSideTRS("Top",_nbLayers,false);
}

//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::createSideTRS(const std::string& name,
             const CFuint& layer,
             const bool& bottom)
{
  cf_assert(_oldElemNode.nbRows() == _oldElemState.nbRows());

  const CFuint nbElemsPerLayer = _oldElemNode.nbRows();

  const CFuint nbTRinTRS = 1;
  const vector<CFuint> geomsInTR(nbTRinTRS,nbElemsPerLayer);

  CFmeshReaderWriterSource& data = *(_data.get());

  const CFuint iTRS = data.getNbTRSs();

  SafePtr<vector<std::string> > nameTRS = data.getNameTRS();
  SafePtr<vector<CFuint> > nbTRs = data.getNbTRs();
  SafePtr<vector<vector<CFuint> > > nbGeomEntsPerTR =
    data.getNbGeomEntsPerTR();
  SafePtr<vector<CFGeoEnt::Type> > geomType = data.getGeomType();
  SafePtr<vector<TRGeoConn> > geoConn = data.getGeoConn();

  nameTRS->push_back(name);
  data.setNbTRSs(data.getNbTRSs() + 1);
  nbTRs->push_back(nbTRinTRS);
  nbGeomEntsPerTR->push_back(geomsInTR);
  geomType->push_back(CFGeoEnt::FACE);

  geoConn->resize(data.getNbTRSs());

  (*geoConn)[iTRS].resize(nbTRinTRS);
  (*geoConn)[iTRS].front().resize(nbElemsPerLayer);

  GeoConn& trCon = (*geoConn)[iTRS].front();

  for(CFuint iGeo = 0; iGeo < nbElemsPerLayer; ++iGeo) {

    // nodes
    const CFuint nbNodesInGeo = _oldElemNode.nbCols(iGeo);
    trCon[iGeo].first.resize(nbNodesInGeo);
    for (CFuint iNode = 0; iNode < nbNodesInGeo; ++iNode) {
      trCon[iGeo].first[iNode] = _oldElemNode(iGeo,iNode) + (layer * _nbNodesPerLayer);
    }

    // states
    const CFuint nbStatesInGeo = _oldElemState.nbCols(iGeo);
    trCon[iGeo].second.resize(nbStatesInGeo);
    for (CFuint iState = 0; iState < nbStatesInGeo; ++iState) {
      if(bottom) trCon[iGeo].second[iState] = _oldElemState(iGeo,iState) + (layer * _nbStatesPerLayer);
      else trCon[iGeo].second[iState] = _oldElemState(iGeo,iState) + ((layer-1) * _nbStatesPerLayer);
    }

    if(bottom) {
      swap(trCon[iGeo].first[1],trCon[iGeo].first[2]);
      //swap(trCon[iGeo].second[1],trCon[iGeo].second[2]);
    }

  }
}


//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::splitPrism(vector<CFuint> prism)
{
  CFAUTOTRACE;

  // unused // const CFuint nbTetrasinPrism = 3;
  // unused // const CFuint nbNodesinTetra  = 4;
  const CFuint nbNodesinPrism  = 6;

  // Prism in notations as in article
  vector<CFuint> p(nbNodesinPrism + 1);
  // Indirections
  vector<CFuint> ii(nbNodesinPrism + 1);

  for (CFuint i=0; i<nbNodesinPrism ; ++i){
    p[i+1]=prism[i];
    }

  //create indirections
  if ( p[1]<p[2] && p[1]<p[3] && p[1]<p[4] && p[1]<p[5] && p[1]<p[6] ) {
    ii[1]=1; ii[2]=2; ii[3]=3; ii[4]=4; ii[5]=5; ii[6]=6;
    }
  else // 2
    if ( p[2]<p[1] && p[2]<p[3] && p[2]<p[4] && p[2]<p[5] && p[2]<p[6] ) {
      ii[1]=2; ii[2]=3; ii[3]=1; ii[4]=5; ii[5]=6; ii[6]=4;
      }
  else // 3
  if ( p[3]<p[1] && p[3]<p[2] && p[3]<p[4] && p[3]<p[5] && p[3]<p[6] ) {
      ii[1]=3; ii[2]=1; ii[3]=2; ii[4]=6; ii[5]=4; ii[6]=5;
      }
  else // 4
    if ( p[4]<p[1] && p[4]<p[2] && p[4]<p[3] && p[4]<p[5] && p[4]<p[6] ) {
      ii[1]=4; ii[2]=6; ii[3]=5; ii[4]=1; ii[5]=3; ii[6]=2;
      }
  else // 5
    if ( p[5]<p[1] && p[5]<p[2] && p[5]<p[3] && p[5]<p[4] && p[5]<p[6] ) {
      ii[1]=5; ii[2]=4; ii[3]=6; ii[4]=2; ii[5]=1; ii[6]=3;
    }
  else // 6
    if ( p[6]<p[1] && p[6]<p[2] && p[6]<p[3] && p[6]<p[4] && p[6]<p[5] ) {
      ii[1]=6; ii[2]=5; ii[3]=4; ii[4]=3; ii[5]=2; ii[6]=1;
    }

  if ( min( p[ii[2]],p[ii[6]] ) < min( p[ii[3]],p[ii[5]] ) ) {

    _newTetras[0][0] = p[ii[1]];
    _newTetras[0][1] = p[ii[2]];
    _newTetras[0][2] = p[ii[3]];
    _newTetras[0][3] = p[ii[6]];

    _newTetras[1][0] = p[ii[1]];
    _newTetras[1][1] = p[ii[2]];
    _newTetras[1][2] = p[ii[6]];
    _newTetras[1][3] = p[ii[5]];

    _newTetras[2][0] = p[ii[1]];
    _newTetras[2][1] = p[ii[5]];
    _newTetras[2][2] = p[ii[6]];
    _newTetras[2][3] = p[ii[4]];
  }
  else{
  if ( min( p[ii[3]],p[ii[5]] ) < min( p[ii[2]],p[ii[6]] ) ) {

    _newTetras[0][0] = p[ii[1]];
    _newTetras[0][1] = p[ii[2]];
    _newTetras[0][2] = p[ii[3]];
    _newTetras[0][3] = p[ii[5]];

    _newTetras[1][0] = p[ii[1]];
    _newTetras[1][1] = p[ii[5]];
    _newTetras[1][2] = p[ii[3]];
    _newTetras[1][3] = p[ii[6]];

    _newTetras[2][0] = p[ii[1]];
    _newTetras[2][1] = p[ii[5]];
    _newTetras[2][2] = p[ii[6]];
    _newTetras[2][3] = p[ii[4]];
  }
  else {
    CFout << "Problem in Prism Splitting" << "\n";
    for (CFuint i=0; i < 6 ; ++i) CFout << "Prism: " << prism[i] << "\n";
    cf_assert(0);
  }
  }
}

//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::splitQuads(vector<CFuint> quad)
{
  CFAUTOTRACE;

  // unused // const CFuint nbNodesinTetra  = 4;

  if ( min( quad[0],quad[2] ) < min( quad[1],quad[3] ) ) {

    _newTriag[0][0] = quad[0];
    _newTriag[0][1] = quad[1];
    _newTriag[0][2] = quad[2];

    _newTriag[1][0] = quad[0];
    _newTriag[1][1] = quad[2];
    _newTriag[1][2] = quad[3];

  }
  else{
  if ( min( quad[1],quad[3] ) < min( quad[0],quad[2] ) ) {

    _newTriag[0][0] = quad[1];
    _newTriag[0][1] = quad[2];
    _newTriag[0][2] = quad[3];

    _newTriag[1][0] = quad[1];
    _newTriag[1][1] = quad[3];
    _newTriag[1][2] = quad[0];
  }
  else {
    for (CFuint i=0; i < 4 ; ++i) CFout << "Quad: " << quad[i] << "\n";
    cf_assert(0);
  }
  }
}

//////////////////////////////////////////////////////////////////////////////

void Extruder2DDGM::randomNodes()
{
  srand(time(0));
  CFmeshReaderWriterSource& data = *(_data.get());
  SafePtr< vector<RealVector> > nodes  = data.getNodeList();
  const CFuint z = 2;
  for (CFuint iNode=_nbNodesPerLayer;iNode < nodes->size() - _nbNodesPerLayer;iNode++)
  {
    (*nodes)[iNode][z]=((rand() % 10000)/10000.0 -0.5)*_zDelta/2.0 + _zDelta*(iNode / _nbNodesPerLayer);
  }
}

//////////////////////////////////////////////////////////////////////////////

    } // namespace CFmeshExtruder

  } // namespace IO

} // namespace COOLFluiD

//////////////////////////////////////////////////////////////////////////////
