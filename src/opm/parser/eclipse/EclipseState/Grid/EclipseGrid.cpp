/*
  Copyright 2014 Statoil ASA.

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#define _USE_MATH_DEFINES
#include <cmath>
#include <numeric>

#include <iostream>
#include <tuple>
#include <functional>

#include <opm/common/OpmLog/OpmLog.hpp>
#include <opm/common/utility/numeric/calculateCellVol.hpp>

#include <opm/io/eclipse/EclFile.hpp>
#include <opm/io/eclipse/EclOutput.hpp>

#include <opm/parser/eclipse/Deck/Section.hpp>
#include <opm/parser/eclipse/Deck/Deck.hpp>
#include <opm/parser/eclipse/Deck/DeckItem.hpp>
#include <opm/parser/eclipse/Deck/DeckKeyword.hpp>
#include <opm/parser/eclipse/Deck/DeckRecord.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/NNC.hpp>

#include <opm/parser/eclipse/Units/UnitSystem.hpp>

#include <opm/parser/eclipse/Parser/ParserKeywords/A.hpp>
#include <opm/parser/eclipse/Parser/ParserKeywords/C.hpp>
#include <opm/parser/eclipse/Parser/ParserKeywords/D.hpp>
#include <opm/parser/eclipse/Parser/ParserKeywords/G.hpp>
#include <opm/parser/eclipse/Parser/ParserKeywords/I.hpp>
#include <opm/parser/eclipse/Parser/ParserKeywords/M.hpp>
#include <opm/parser/eclipse/Parser/ParserKeywords/P.hpp>
#include <opm/parser/eclipse/Parser/ParserKeywords/R.hpp>
#include <opm/parser/eclipse/Parser/ParserKeywords/S.hpp>
#include <opm/parser/eclipse/Parser/ParserKeywords/T.hpp>
#include <opm/parser/eclipse/Parser/ParserKeywords/Z.hpp>

#include <opm/parser/eclipse/EclipseState/Grid/EclipseGrid.hpp>


namespace Opm {


EclipseGrid::EclipseGrid(std::array<int, 3>& dims ,
                         const std::vector<double>& coord ,
                         const std::vector<double>& zcorn ,
                         const int * actnum,
                         const double * mapaxes)
    : GridDims(dims),
      m_minpvMode(MinpvMode::ModeEnum::Inactive),
      m_pinch("PINCH"),
      m_pinchoutMode(PinchMode::ModeEnum::TOPBOT),
      m_multzMode(PinchMode::ModeEnum::TOP)
{
    initCornerPointGrid( dims, coord , zcorn , actnum , mapaxes );
}

/**
   Will create an EclipseGrid instance based on an existing
   GRID/EGRID file.
*/


EclipseGrid::EclipseGrid(const std::string& fileName )
    : GridDims(),
      m_minpvMode(MinpvMode::ModeEnum::Inactive),
      m_pinch("PINCH"),
      m_pinchoutMode(PinchMode::ModeEnum::TOPBOT),
      m_multzMode(PinchMode::ModeEnum::TOP)
{

    Opm::EclIO::EclFile egridfile(fileName);

    m_useActnumFromGdfile=true;

    initGridFromEGridFile(egridfile, fileName);

    resetACTNUM(m_actnum);

    calculateGeometryData();

}


EclipseGrid::EclipseGrid(size_t nx, size_t ny , size_t nz,
                         double dx, double dy, double dz)
    : GridDims(nx, ny, nz),
      m_minpvMode(MinpvMode::ModeEnum::Inactive),
      m_pinch("PINCH"),
      m_pinchoutMode(PinchMode::ModeEnum::TOPBOT),
      m_multzMode(PinchMode::ModeEnum::TOP)
{

    m_coord.reserve((nx+1)*(ny+1)*6);

    for (size_t j = 0; j < (ny+1); j++) {
        for (size_t i = 0; i < (nx+1); i++) {
            m_coord.push_back(i*dx);
            m_coord.push_back(j*dy);
            m_coord.push_back(0.0);
            m_coord.push_back(i*dx);
            m_coord.push_back(j*dy);
            m_coord.push_back(nz*dz);
        }
    }

    m_zcorn.assign(nx*ny*nz*8, 0);

    for (size_t k = 0; k < nz ; k++) {
        for (size_t j = 0; j < ny ; j++) {
            for (size_t i = 0; i < nx ; i++) {

                // top face of cell
                int zind = i*2 + j*nx*4 + k*nx*ny*8;

                double zt = k*dz;
                double zb = (k+1)*dz;

                m_zcorn[zind] = zt;
                m_zcorn[zind + 1] = zt;

                zind = zind + nx*2;

                m_zcorn[zind] = zt;
                m_zcorn[zind + 1] = zt;

                // bottom face of cell
                zind = i*2 + j*nx*4 + k*nx*ny*8 + nx*ny*4;

                m_zcorn[zind] = zb;
                m_zcorn[zind + 1] = zb;

                zind = zind + nx*2;

                m_zcorn[zind] = zb;
                m_zcorn[zind + 1] = zb;
            }
        }
    }

    resetACTNUM();
    calculateGeometryData();
}

EclipseGrid::EclipseGrid(const EclipseGrid& src, const double* zcorn, const std::vector<int>& actnum)
    : EclipseGrid(src)
{

    if (zcorn != nullptr) {
        std::array<int, 3> dims = getNXYZ();

        size_t sizeZcorn = dims[0]*dims[1]*dims[2]*8;

        for (size_t n=0; n < sizeZcorn; n++) {
            m_zcorn[n] = zcorn[n];
        }

        ZcornMapper mapper( getNX(), getNY(), getNZ());
        zcorn_fixed = mapper.fixupZCORN( m_zcorn );

        calculateGeometryData();
    }

    resetACTNUM(actnum);
}

EclipseGrid::EclipseGrid(const EclipseGrid& src, const std::vector<int>& actnum)
        : EclipseGrid( src , nullptr , actnum )
{ }

/*
  This is the main EclipseGrid constructor, it will inspect the
  input Deck for grid keywords, either the corner point keywords
  COORD and ZCORN, or the various rectangular keywords like DX,DY
  and DZ.

  Actnum is treated specially:

    1. If an actnum pointer is passed in that should be a pointer
       to 0 and 1 values which will be used as actnum mask.

    2. If the actnum pointer is not present the constructor will
       look in the deck for an actnum keyword, and use that if it
       is found. This is a best effort which will work in many
       cases, but if the ACTNUM keyword is manipulated in the deck
       those manipulations will be silently lost; if the ACTNUM
       keyword has size different from nx*ny*nz it will also be
       silently ignored.

  With a mutable EclipseGrid instance you can later call the
  EclipseGrid::resetACTNUM() method when you have complete actnum
  information. The EclipseState based construction of EclipseGrid
  is a two-pass operation, which guarantees that actnum is handled
  correctly.
*/



EclipseGrid::EclipseGrid(const Deck& deck, const int * actnum)
    : GridDims(deck),
      m_minpvMode(MinpvMode::ModeEnum::Inactive),
      m_pinch("PINCH"),
      m_pinchoutMode(PinchMode::ModeEnum::TOPBOT),
      m_multzMode(PinchMode::ModeEnum::TOP)
{

    if (deck.hasKeyword("GDFILE")){

        if (deck.hasKeyword("COORD")){
            throw std::invalid_argument("COORD can't be used together with GDFILE");
        }

        if (deck.hasKeyword("ZCORN")){
            throw std::invalid_argument("ZCORN can't be used together with GDFILE");
        }

        if (deck.hasKeyword("ACTNUM")){
            if (keywInputBeforeGdfile(deck, "ACTNUM"))  {
                m_useActnumFromGdfile = true;
            }
        } else {
            m_useActnumFromGdfile = true;
        }

    }


    const std::array<int, 3> dims = getNXYZ();

    initGrid(dims, deck);

    if (deck.hasKeyword("MAPUNITS")){
       if ((m_mapunits =="") || ( !keywInputBeforeGdfile(deck, "MAPUNITS"))) {
            const auto& record = deck.getKeyword<ParserKeywords::MAPUNITS>( ).getStringData();
            m_mapunits = record[0];
       }
    }

    if (deck.hasKeyword("MAPAXES")){
       if ((m_mapaxes.size() == 0) || ( !keywInputBeforeGdfile(deck, "MAPAXES"))) {
          const Opm::DeckKeyword& mapaxesKeyword = deck.getKeyword<ParserKeywords::MAPAXES>();
          m_mapaxes.resize(6);
          for (size_t n=0; n < 6; n++){
             m_mapaxes[n] = mapaxesKeyword.getRecord(0).getItem(n).get<double>(0);
          }
       }
    }

    if (actnum != nullptr) {

        size_t nCells = dims[0]*dims[1]*dims[2];
        std::vector<int> actnumVector(actnum, actnum + nCells);

        resetACTNUM( actnumVector);
    } else {

        if (m_useActnumFromGdfile){
            // actnum already reset in initBinaryGrid
        } else if (deck.hasKeyword<ParserKeywords::ACTNUM>()) {

            const auto& actnumData = deck.getKeyword<ParserKeywords::ACTNUM>().getIntData();

            if (actnumData.size() == getCartesianSize()) {
                resetACTNUM( actnumData);
            } else {
                const std::string msg = "The ACTNUM keyword has " + std::to_string( actnumData.size() ) + " elements - expected : " + std::to_string( getCartesianSize()) + " - ignored.";
                OpmLog::warning(msg);

                resetACTNUM( );
            }

        } else {

            resetACTNUM();
        }
    }

    calculateGeometryData();
}


    bool EclipseGrid::circle( ) const{
        return this->m_circle;
    }

    void EclipseGrid::initGrid( const std::array<int, 3>& dims, const Deck& deck) {

        if (deck.hasKeyword<ParserKeywords::RADIAL>()) {
            initCylindricalGrid( dims, deck );
        } else {
            if (hasCornerPointKeywords(deck)) {
                initCornerPointGrid(dims , deck);
            } else if (hasCartesianKeywords(deck)) {
                initCartesianGrid(dims , deck);
            } else if (hasGDFILE(deck)) {
                initBinaryGrid(deck);
            } else {
                throw std::invalid_argument("EclipseGrid needs cornerpoint or cartesian keywords.");
            }
        }

        if (deck.hasKeyword<ParserKeywords::PINCH>()) {
            const auto& record = deck.getKeyword<ParserKeywords::PINCH>( ).getRecord(0);
            const auto& item = record.getItem<ParserKeywords::PINCH::THRESHOLD_THICKNESS>( );
            m_pinch.setValue( item.getSIDouble(0) );

            auto pinchoutString = record.getItem<ParserKeywords::PINCH::PINCHOUT_OPTION>().get< std::string >(0);
            m_pinchoutMode = PinchMode::PinchModeFromString(pinchoutString);

            auto multzString = record.getItem<ParserKeywords::PINCH::MULTZ_OPTION>().get< std::string >(0);
            m_multzMode = PinchMode::PinchModeFromString(multzString);
        }

        if (deck.hasKeyword<ParserKeywords::MINPV>() && deck.hasKeyword<ParserKeywords::MINPVFIL>()) {
            throw std::invalid_argument("Can not have both MINPV and MINPVFIL in deck.");
        }

        m_minpvVector.resize(getCartesianSize(), 0.0);
        if (deck.hasKeyword<ParserKeywords::MINPV>()) {
            const auto& record = deck.getKeyword<ParserKeywords::MINPV>( ).getRecord(0);
            const auto& item = record.getItem<ParserKeywords::MINPV::VALUE>( );
            std::fill(m_minpvVector.begin(), m_minpvVector.end(), item.getSIDouble(0));
            m_minpvMode = MinpvMode::ModeEnum::EclSTD;
        } else if(deck.hasKeyword<ParserKeywords::MINPVV>()) {
            // We should use the grid properties to support BOX, but then we need the eclipseState
            const auto& record = deck.getKeyword<ParserKeywords::MINPVV>( ).getRecord(0);
            m_minpvVector =record.getItem(0).getSIDoubleData();
            m_minpvMode = MinpvMode::ModeEnum::EclSTD;
        }

        if (deck.hasKeyword<ParserKeywords::MINPVFIL>()) {
            const auto& record = deck.getKeyword<ParserKeywords::MINPVFIL>( ).getRecord(0);
            const auto& item = record.getItem<ParserKeywords::MINPVFIL::VALUE>( );
            std::fill(m_minpvVector.begin(), m_minpvVector.end(), item.getSIDouble(0));
            m_minpvMode = MinpvMode::ModeEnum::OpmFIL;
        }
    }

    void EclipseGrid::initGridFromEGridFile(Opm::EclIO::EclFile& egridfile, std::string fileName){

        if (!egridfile.hasKey("GRIDHEAD")) {
            throw std::invalid_argument("file: " + fileName + " is not a valid egrid file, GRIDHEAD not found");
        }

        if (!egridfile.hasKey("COORD")) {
            throw std::invalid_argument("file: " + fileName + " is not a valid egrid file, COORD not found");
        }

        if (!egridfile.hasKey("ZCORN")) {
            throw std::invalid_argument("file: " + fileName + " is not a valid egrid file, ZCORN not found");
        }

        if (!egridfile.hasKey("GRIDUNIT")) {
            throw std::invalid_argument("file: " + fileName + " is not a valid egrid file, ZCORN not found");
        }

        const std::vector<std::string>& gridunit = egridfile.get<std::string>("GRIDUNIT");

        const std::vector<int>& gridhead = egridfile.get<int>("GRIDHEAD");
        const std::array<int, 3> dims = {gridhead[1], gridhead[2], gridhead[3]};

        m_nx=dims[0];
        m_ny=dims[1];
        m_nz=dims[2];

        const std::vector<float>& coord_f = egridfile.get<float>("COORD");
        const std::vector<float>& zcorn_f = egridfile.get<float>("ZCORN");

        m_coord.assign(coord_f.begin(), coord_f.end());
        m_zcorn.assign(zcorn_f.begin(), zcorn_f.end());

        if (gridunit[0] != "METRES") {

            const auto length = ::Opm::UnitSystem::measure::length;

            if (gridunit[0] == "FEET"){
                Opm::UnitSystem units(Opm::UnitSystem::UnitType::UNIT_TYPE_FIELD );
                units.to_si(length, m_coord);
                units.to_si(length, m_zcorn);
            } else if (gridunit[0] == "CM"){
                Opm::UnitSystem units(Opm::UnitSystem::UnitType::UNIT_TYPE_LAB );
                units.to_si(length, m_coord);
                units.to_si(length, m_zcorn);
            } else {
                std::string message = "gridunit '" + gridunit[0] + "' doesn't correspong to a valid unit system";
                throw std::invalid_argument(message);
            }
        }

        if ((egridfile.hasKey("ACTNUM")) && (m_useActnumFromGdfile)) {
            const std::vector<int>& actnum  = egridfile.get<int>("ACTNUM");
            resetACTNUM( actnum );
        }

        if (egridfile.hasKey("MAPAXES")) {
            const std::vector<float>& mapaxes_f = egridfile.get<float>("MAPAXES");
            m_mapaxes.assign(mapaxes_f.begin(), mapaxes_f.end());
        }

        if (egridfile.hasKey("MAPUNITS")) {
            const std::vector<std::string>& mapunits = egridfile.get<std::string>("MAPUNITS");
            m_mapunits=mapunits[0];
        }

        ZcornMapper mapper( getNX(), getNY(), getNZ());
        zcorn_fixed = mapper.fixupZCORN( m_zcorn );
    }

    bool EclipseGrid::keywInputBeforeGdfile(const Deck& deck, const std::string keyword) const {

        std::vector<std::string> keywordList;
        keywordList.reserve(deck.size());

        for (size_t n=0;n<deck.size();n++){
            DeckKeyword kw = deck.getKeyword( n );
            keywordList.push_back(kw.name());
        }

        int indKeyw = -1;
        int indGdfile = -1;

        for (size_t i = 0; i < keywordList.size(); i++){
            if (keywordList[i]=="GDFILE"){
                indGdfile=i;
            }

            if (keywordList[i]==keyword){
                indKeyw=i;
            }
        }

        if (indGdfile == -1) {
            throw std::runtime_error("keyword GDFILE not found in deck ");
        }

        if (indKeyw == -1) {
            throw std::runtime_error("keyword " + keyword + " not found in deck ");
        }

        return indKeyw < indGdfile;
    }


    size_t EclipseGrid::activeIndex(size_t i, size_t j, size_t k) const {

        return activeIndex( getGlobalIndex( i,j,k ));
    }

    size_t EclipseGrid::activeIndex(size_t globalIndex) const {

        if (m_global_to_active[ globalIndex] == -1) {
            throw std::invalid_argument("Input argument does not correspond to an active cell");
        }

        return m_global_to_active[ globalIndex];
    }

    /**
       Observe: the input argument is assumed to be in the space
       [0,num_active).
    */

    size_t EclipseGrid::getGlobalIndex(size_t active_index) const {

        return m_active_to_global.at(active_index);
    }

    size_t EclipseGrid::getGlobalIndex(size_t i, size_t j, size_t k) const {

        return GridDims::getGlobalIndex(i,j,k);
    }


    bool EclipseGrid::isPinchActive( ) const {
        return m_pinch.hasValue();
    }

    double EclipseGrid::getPinchThresholdThickness( ) const {
        return m_pinch.getValue();
    }

    PinchMode::ModeEnum EclipseGrid::getPinchOption( ) const {
        return m_pinchoutMode;
    }

    PinchMode::ModeEnum EclipseGrid::getMultzOption( ) const {
        return m_multzMode;
    }

    MinpvMode::ModeEnum EclipseGrid::getMinpvMode() const {
        return m_minpvMode;
    }

    const std::vector<double>& EclipseGrid::getMinpvVector( ) const {
        return m_minpvVector;
    }

    void EclipseGrid::initBinaryGrid(const Deck& deck) {

        const DeckKeyword& gdfile_kw = deck.getKeyword("GDFILE");
        const std::string& gdfile_arg = gdfile_kw.getRecord(0).getItem("filename").get<std::string>(0);
        std::string filename = deck.makeDeckPath(gdfile_arg);

        Opm::EclIO::EclFile egridfile(filename);

        initGridFromEGridFile(egridfile, filename);
    }

    void EclipseGrid::initCartesianGrid(const std::array<int, 3>& dims , const Deck& deck) {

        if (hasDVDEPTHZKeywords( deck )) {
            initDVDEPTHZGrid( dims , deck );
        } else if (hasDTOPSKeywords(deck)) {
            initDTOPSGrid( dims ,deck );
        } else {
            throw std::invalid_argument("Tried to initialize cartesian grid without all required keywords");
        }
    }

    void EclipseGrid::initDVDEPTHZGrid(const std::array<int, 3>& dims, const Deck& deck) {

        const std::vector<double>& DXV = deck.getKeyword<ParserKeywords::DXV>().getSIDoubleData();
        const std::vector<double>& DYV = deck.getKeyword<ParserKeywords::DYV>().getSIDoubleData();
        const std::vector<double>& DZV = deck.getKeyword<ParserKeywords::DZV>().getSIDoubleData();
        const std::vector<double>& DEPTHZ = deck.getKeyword<ParserKeywords::DEPTHZ>().getSIDoubleData();

        assertVectorSize( DEPTHZ , static_cast<size_t>( (dims[0] + 1)*(dims[1] +1 )) , "DEPTHZ");
        assertVectorSize( DXV    , static_cast<size_t>( dims[0] ) , "DXV");
        assertVectorSize( DYV    , static_cast<size_t>( dims[1] ) , "DYV");
        assertVectorSize( DZV    , static_cast<size_t>( dims[2] ) , "DZV");

        m_coord = makeCoordDxvDyvDzvDepthz(dims, DXV, DYV, DZV, DEPTHZ);
        m_zcorn = makeZcornDzvDepthz(dims, DZV, DEPTHZ);

        ZcornMapper mapper( getNX(), getNY(), getNZ());
        zcorn_fixed = mapper.fixupZCORN( m_zcorn );
    }

    void EclipseGrid::initDTOPSGrid(const std::array<int, 3>& dims , const Deck& deck) {

        std::vector<double> DX = createDVector( dims , 0 , "DX" , "DXV" , deck);
        std::vector<double> DY = createDVector( dims , 1 , "DY" , "DYV" , deck);
        std::vector<double> DZ = createDVector( dims , 2 , "DZ" , "DZV" , deck);
        std::vector<double> TOPS = createTOPSVector( dims , DZ , deck );

        m_coord = makeCoordDxDyDzTops(dims, DX, DY, DZ, TOPS);
        m_zcorn = makeZcornDzTops(dims, DZ, TOPS);

        ZcornMapper mapper( getNX(), getNY(), getNZ());
        zcorn_fixed = mapper.fixupZCORN( m_zcorn );
    }

    void EclipseGrid::calculateGeometryData() {

        const std::array<int, 3> dims = getNXYZ();
        int nCells = getCartesianSize();

        std::array<double,8> X = {0.0};
        std::array<double,8> Y = {0.0};
        std::array<double,8> Z = {0.0};

        m_volume.clear();
        m_cellCenter.clear();
        m_dx.clear();
        m_dy.clear();
        m_dz.clear();
        m_depth.clear();

        m_volume.resize(nCells);
        m_cellCenter.resize(nCells);
        m_dx.resize(nCells);
        m_dy.resize(nCells);
        m_dz.resize(nCells);
        m_depth.resize(nCells);

        std::array<int, 3> ijk;
        size_t n = 0;

        for (size_t k = 0; k< getNZ(); k++) {
            for (size_t j = 0; j< getNY(); j++) {
                for (size_t i = 0; i< getNX(); i++) {

                    ijk[0] = i;
                    ijk[1] = j;
                    ijk[2] = k;

                    getCellCorners(ijk, dims, X, Y, Z );

                    m_volume[n] = calculateCellVol(X, Y, Z);

                    std::array<double, 3> center;

                    center[0]=std::accumulate(X.begin(), X.end(), 0.0) / 8.0;
                    center[1]=std::accumulate(Y.begin(), Y.end(), 0.0) / 8.0;
                    center[2]=std::accumulate(Z.begin(), Z.end(), 0.0) / 8.0;

                    m_cellCenter[n] = center;

                    // calculate dx

                    double x2 = (X[1]+X[3]+X[5]+X[7])/4.0;
                    double y2 = (Y[1]+Y[3]+Y[5]+Y[7])/4.0;

                    double x1 = (X[0]+X[2]+X[4]+X[6])/4.0;
                    double y1 = (Y[0]+Y[2]+Y[4]+Y[6])/4.0;

                    double dx = sqrt(pow((x2-x1), 2.0) + pow((y2-y1), 2.0) );

                    // calculate dy

                    x2 = (X[2]+X[3]+X[6]+X[7])/4.0;
                    y2 = (Y[2]+Y[3]+Y[6]+Y[7])/4.0;

                    x1 = (X[0]+X[1]+X[4]+X[5])/4.0;
                    y1 = (Y[0]+Y[1]+Y[4]+Y[5])/4.0;

                    double dy = sqrt(pow((x2-x1), 2.0) + pow((y2-y1), 2.0));

                    // calculate dz

                    double z2 = (Z[4]+Z[5]+Z[6]+Z[7])/4.0;
                    double z1 = (Z[0]+Z[1]+Z[2]+Z[3])/4.0;

                    double dz = z2-z1;

                    m_dx[n] = dx;
                    m_dy[n] = dy;
                    m_dz[n] = dz;

                    m_depth[n] =  (z2 + z1) / 2.0;

                    n++;
                }
            }
        }
    }

    void EclipseGrid::getCellCorners(const std::array<int, 3>& ijk, const std::array<int, 3>& dims,
                                     std::array<double,8>& X,
                                     std::array<double,8>& Y,
                                     std::array<double,8>& Z) const
    {

        std::array<int, 8> zind;
        std::array<int, 4> pind;

        // calculate indices for grid pillars in COORD arrray
        const size_t p_offset = ijk[1]*(dims[0]+1)*6 + ijk[0]*6;

        pind[0] = p_offset;
        pind[1] = p_offset + 6;
        pind[2] = p_offset + (dims[0]+1)*6;
        pind[3] = pind[2] + 6;

        // get depths from zcorn array in ZCORN array
        const size_t z_offset = ijk[2]*dims[0]*dims[1]*8 + ijk[1]*dims[0]*4 + ijk[0]*2;

        zind[0] = z_offset;
        zind[1] = z_offset + 1;
        zind[2] = z_offset + dims[0]*2;
        zind[3] = zind[2] + 1;

        for (int n = 0; n < 4; n++)
            zind[n+4] = zind[n] + dims[0]*dims[1]*4;


        for (int n = 0; n< 8; n++)
           Z[n] = m_zcorn[zind[n]];


        for (int  n=0; n<4; n++) {
            double xt = m_coord[pind[n]];
            double yt = m_coord[pind[n] + 1];
            double zt = m_coord[pind[n] + 2];

            double xb = m_coord[pind[n] + 3];
            double yb = m_coord[pind[n] + 4];
            double zb = m_coord[pind[n]+5];

            if (zt == zb) {
                X[n] = xt;
                X[n + 4] = xt;

                Y[n] = yt;
                Y[n + 4] = yt;
            } else {
                X[n] = xt + (xb-xt) / (zt-zb) * (zt - Z[n]);
                X[n+4] = xt + (xb-xt) / (zt-zb) * (zt-Z[n+4]);

                Y[n] = yt+(yb-yt)/(zt-zb)*(zt-Z[n]);
                Y[n+4] = yt+(yb-yt)/(zt-zb)*(zt-Z[n+4]);
            }
        }
    }

    std::vector<double> EclipseGrid::makeCoordDxvDyvDzvDepthz(const std::array<int, 3>& dims, const std::vector<double>& dxv, const std::vector<double>& dyv, const std::vector<double>& dzv, const std::vector<double>& depthz) const {

        std::vector<double> coord;
        coord.reserve((dims[0]+1)*(dims[1]+1)*6);

        std::vector<double> x(dims[0] + 1, 0.0);
        std::partial_sum(dxv.begin(), dxv.end(), x.begin() + 1);

        std::vector<double> y(dims[1] + 1, 0.0);
        std::partial_sum(dyv.begin(), dyv.end(), y.begin() + 1);

        std::vector<double> z(dims[2] + 1, 0.0);
        std::partial_sum(dzv.begin(), dzv.end(), z.begin() + 1);


        for (int j = 0; j < dims[1] + 1; j++) {
            for (int i = 0; i<dims[0] + 1; i++) {

                const double x0 = x[i];
                const double y0 = y[j];

                size_t ind = i+j*(dims[0]+1);

                double zb = depthz[ind] + z[dims[2]];

                coord.push_back(x0);
                coord.push_back(y0);
                coord.push_back(depthz[ind]);
                coord.push_back(x0);
                coord.push_back(y0);
                coord.push_back(zb);
            }
        }

        return coord;
    }

    std::vector<double> EclipseGrid::makeZcornDzvDepthz(const std::array<int, 3>& dims, const std::vector<double>& dzv, const std::vector<double>& depthz) const {

        std::vector<double> zcorn;
        size_t sizeZcorn = dims[0]*dims[1]*dims[2]*8;

        zcorn.assign (sizeZcorn, 0.0);

        std::vector<double> z(dims[2] + 1, 0.0);
        std::partial_sum(dzv.begin(), dzv.end(), z.begin() + 1);


        for (int k = 0; k < dims[2]; k++) {
            for (int j = 0; j < dims[1]; j++) {
                for (int i = 0; i < dims[0]; i++) {

                    const double z0 = z[k];

                    // top face of cell
                    int zind = i*2 + j*dims[0]*4 + k*dims[0]*dims[1]*8;

                    zcorn[zind] = depthz[i+j*(dims[0]+1)] + z0;
                    zcorn[zind + 1] = depthz[i+j*(dims[0]+1) +1 ] + z0;

                    zind = zind + dims[0]*2;

                    zcorn[zind] = depthz[i+(j+1)*(dims[0]+1)] + z0;
                    zcorn[zind + 1] = depthz[i+(j+1)*(dims[0]+1) + 1] + z0;

                    // bottom face of cell
                    zind = i*2 + j*dims[0]*4 + k*dims[0]*dims[1]*8 + dims[0]*dims[1]*4;

                    zcorn[zind] = depthz[i+j*(dims[0]+1)] + z0 + dzv[k];
                    zcorn[zind + 1] = depthz[i+j*(dims[0]+1) +1 ] + z0 + dzv[k];

                    zind = zind + dims[0]*2;

                    zcorn[zind] = depthz[i+(j+1)*(dims[0]+1)] + z0 + dzv[k];
                    zcorn[zind + 1] = depthz[i+(j+1)*(dims[0]+1) + 1] + z0 + dzv[k];
                }
            }
        }

        return zcorn;
    }

    std::vector<double> EclipseGrid::makeCoordDxDyDzTops(const std::array<int, 3>& dims , const std::vector<double>& dx, const std::vector<double>& dy, const std::vector<double>& dz, const std::vector<double>& tops) const {

        std::vector<double> coord;
        coord.reserve((dims[0]+1)*(dims[1]+1)*6);

        for (int j = 0; j < dims[1]; j++) {

            double y0 = 0;
            double zt = tops[0];
            double zb = zt + sumKdir(0, 0, dims, dz);

            if (j == 0) {
                double x0 = 0.0;

                coord.push_back(x0);
                coord.push_back(y0);
                coord.push_back(zt);
                coord.push_back(x0);
                coord.push_back(y0);
                coord.push_back(zb);

                for (int i = 0; i < dims[0]; i++) {

                    size_t ind = i+j*dims[0]+1;

                    if (i == (dims[0]-1)) {
                        ind=ind-1;
                    }

                    zt = tops[ind];
                    zb = zt + sumKdir(i, j, dims, dz);

                    double xt = x0 + dx[i+j*dims[0]];
                    double xb = sumIdir(i, j, dims[2]-1, dims, dx);

                    coord.push_back(xt);
                    coord.push_back(y0);
                    coord.push_back(zt);
                    coord.push_back(xb);
                    coord.push_back(y0);
                    coord.push_back(zb);

                    x0=xt;
                }
            }

            int ind = (j+1)*dims[0];

            if (j == (dims[1]-1) ) {
                ind = j*dims[0];
            }

            double x0 = 0.0;

            double yt = sumJdir(0, j, 0, dims, dy);
            double yb = sumJdir(0, j, dims[2]-1, dims, dy);

            zt = tops[ind];
            zb = zt + sumKdir(0, j, dims, dz);

            coord.push_back(x0);
            coord.push_back(yt);
            coord.push_back(zt);
            coord.push_back(x0);
            coord.push_back(yb);
            coord.push_back(zb);

            for (int i=0; i < dims[0]; i++) {

                ind = i+(j+1)*dims[0]+1;

                if (j == (dims[1]-1) ) {
                    ind = i+j*dims[0]+1;
                }

                if (i == (dims[0] - 1) ) {
                    ind=ind-1;
                }

                zt = tops[ind];
                zb = zt + sumKdir(i, j, dims, dz);

                double xt=-999;
                double xb;

                if (j == (dims[1]-1) ) {
                    xt = sumIdir(i, j, 0, dims, dx);
                    xb = sumIdir(i, j, dims[2]-1, dims, dx);
                } else {
                    xt = sumIdir(i, j+1, 0, dims, dx);
                    xb = sumIdir(i, j+1, dims[2]-1, dims, dx);
                }

                if (i == (dims[0] - 1) ) {
                    yt = sumJdir(i, j, 0, dims, dy);
                    yb = sumJdir(i, j, dims[2]-1, dims, dy);
                } else {
                    yt = sumJdir(i+1, j, 0, dims, dy);
                    yb = sumJdir(i+1, j, dims[2]-1, dims, dy);
                }

                coord.push_back(xt);
                coord.push_back(yt);
                coord.push_back(zt);
                coord.push_back(xb);
                coord.push_back(yb);
                coord.push_back(zb);
            }
        }

        return coord;
    }

    std::vector<double> EclipseGrid::makeZcornDzTops(const std::array<int, 3>& dims, const std::vector<double>& dz, const std::vector<double>& tops) const {

        std::vector<double> zcorn;
        size_t sizeZcorn = dims[0]*dims[1]*dims[2]*8;

        zcorn.assign (sizeZcorn, 0.0);

        for (int j = 0; j < dims[1]; j++) {
            for (int i = 0; i < dims[0]; i++) {
                int ind = i + j*dims[0];
                double z = tops[ind];

                for (int k = 0; k < dims[2]; k++) {

                    // top face of cell
                    int zind = i*2 + j*dims[0]*4 + k*dims[0]*dims[1]*8;

                    zcorn[zind] = z;
                    zcorn[zind + 1] = z;

                    zind = zind + dims[0]*2;

                    zcorn[zind] = z;
                    zcorn[zind + 1] = z;

                    z = z + dz[i + j*dims[0] + k*dims[0]*dims[1]];

                    // bottom face of cell
                    zind = i*2 + j*dims[0]*4 + k*dims[0]*dims[1]*8 + dims[0]*dims[1]*4;

                    zcorn[zind] = z;
                    zcorn[zind + 1] = z;

                    zind = zind + dims[0]*2;

                    zcorn[zind] = z;
                    zcorn[zind + 1] = z;
                }
            }
        }

        return zcorn;
    }

    double EclipseGrid::sumIdir(int i1, int j, int k, const std::array<int, 3>& dims, const std::vector<double>& dx) const {

        double sum = 0.0;

        for (int i = 0; i <= i1; i++) {
            sum = sum + dx[i + j*dims[0] + k*dims[0]*dims[1]];
        }

        return sum;
    }

    double EclipseGrid::sumJdir(int i, int j1, int k, const std::array<int, 3>& dims, const std::vector<double>& dy) const {

        double sum = 0.0;

        for (int j = 0; j <= j1; j++) {
            sum=sum + dy[i + j*dims[0] + k*dims[0]*dims[1]];
        }

        return sum;
    }

    double EclipseGrid::sumKdir(int i, int j, const std::array<int, 3>& dims, const std::vector<double>& dz) const {

        double sum = 0.0;

        for (int k = 0; k < dims[2]; k++) {
            sum = sum + dz[i + j*dims[0] + k*dims[0]*dims[1]];
        }

        return sum;
    }

    /*
      Limited implementaton - requires keywords: DRV, DTHETAV, DZV and TOPS.
    */

    void EclipseGrid::initCylindricalGrid(const std::array<int, 3>& dims , const Deck& deck)
    {
        // The hasCyindricalKeywords( ) checks according to the
        // eclipse specification. We currently do not support all
        // aspects of cylindrical grids, we therefor have an
        // additional test here, which checks if we have the keywords
        // required by the current implementation.
        if (!hasCylindricalKeywords(deck))
            throw std::invalid_argument("Not all keywords required for cylindrical grids present");

        if (!deck.hasKeyword<ParserKeywords::DTHETAV>())
            throw std::logic_error("The current implementation *must* have theta values specified using the DTHETAV keyword");

        if (!deck.hasKeyword<ParserKeywords::DRV>())
            throw std::logic_error("The current implementation *must* have radial values specified using the DRV keyword");

        if (!deck.hasKeyword<ParserKeywords::DZV>() || !deck.hasKeyword<ParserKeywords::TOPS>())
            throw std::logic_error("The current implementation *must* have vertical cell size specified using the DZV and TOPS keywords");

        const std::vector<double>& drv     = deck.getKeyword<ParserKeywords::DRV>().getSIDoubleData();
        const std::vector<double>& dthetav = deck.getKeyword<ParserKeywords::DTHETAV>().getSIDoubleData();
        const std::vector<double>& dzv     = deck.getKeyword<ParserKeywords::DZV>().getSIDoubleData();
        const std::vector<double>& tops    = deck.getKeyword<ParserKeywords::TOPS>().getSIDoubleData();

        if (drv.size() != static_cast<size_t>(dims[0]))
            throw std::invalid_argument("DRV keyword should have exactly " + std::to_string( dims[0] ) + " elements");

        if (dthetav.size() != static_cast<size_t>(dims[1]))
            throw std::invalid_argument("DTHETAV keyword should have exactly " + std::to_string( dims[1] ) + " elements");

        if (dzv.size() != static_cast<size_t>(dims[2]))
            throw std::invalid_argument("DZV keyword should have exactly " + std::to_string( dims[2] ) + " elements");

        if (tops.size() != static_cast<size_t>(dims[0] * dims[1]))
            throw std::invalid_argument("TOPS keyword should have exactly " + std::to_string( dims[0] * dims[1] ) + " elements");

        {
            double total_angle = 0;
            for (auto theta : dthetav)
                total_angle += theta;

            if (std::abs( total_angle - 360 ) < 0.01)
                m_circle = deck.hasKeyword<ParserKeywords::CIRCLE>();
            else {
                if (total_angle > 360)
                    throw std::invalid_argument("More than 360 degrees rotation - cells will be double covered");
            }
        }

        /*
          Now the data has been validated, now we continue to create
          ZCORN and COORD vectors, and we are done.
        */
        {
            ZcornMapper zm( dims[0], dims[1] , dims[2]);
            CoordMapper cm(dims[0] , dims[1]);
            std::vector<double> zcorn( zm.size() );
            std::vector<double> coord( cm.size() );
            {
                std::vector<double> zk(dims[2]);
                zk[0] = 0;
                for (int k = 1; k < dims[2]; k++)
                    zk[k] = zk[k - 1] + dzv[k - 1];

                for (int k = 0; k < dims[2]; k++) {
                    for (int j = 0; j < dims[1]; j++) {
                        for (int i = 0; i < dims[0]; i++) {
                            size_t tops_value = tops[ i + dims[0] * j];
                            for (size_t c=0; c < 4; c++) {
                                zcorn[ zm.index(i,j,k,c) ]     = zk[k] + tops_value;
                                zcorn[ zm.index(i,j,k,c + 4) ] = zk[k] + tops_value + dzv[k];
                            }
                        }
                    }
                }
            }
            {
                std::vector<double> ri(dims[0] + 1);
                std::vector<double> tj(dims[1] + 1);
                double z1 = *std::min_element( zcorn.begin() , zcorn.end());
                double z2 = *std::max_element( zcorn.begin() , zcorn.end());
                ri[0] = deck.getKeyword<ParserKeywords::INRAD>().getRecord(0).getItem(0).getSIDouble( 0 );
                for (int i = 1; i <= dims[0]; i++)
                    ri[i] = ri[i - 1] + drv[i - 1];

                tj[0] = 0;
                for (int j = 1; j <= dims[1]; j++)
                    tj[j] = tj[j - 1] + dthetav[j - 1];


                for (int j = 0; j <= dims[1]; j++) {
                    /*
                      The theta value is supposed to go counterclockwise, starting at 'twelve o clock'.
                    */
                    double t = M_PI * (90 - tj[j]) / 180;
                    double c = cos( t );
                    double s = sin( t );
                    for (int i = 0; i <= dims[0]; i++) {
                        double r = ri[i];
                        double x = r*c;
                        double y = r*s;

                        coord[ cm.index(i,j,0,0) ] = x;
                        coord[ cm.index(i,j,1,0) ] = y;
                        coord[ cm.index(i,j,2,0) ] = z1;

                        coord[ cm.index(i,j,0,1) ] = x;
                        coord[ cm.index(i,j,1,1) ] = y;
                        coord[ cm.index(i,j,2,1) ] = z2;
                    }
                }
            }
            initCornerPointGrid( dims , coord, zcorn, nullptr, nullptr);
        }
    }


    void EclipseGrid::initCornerPointGrid(const std::array<int,3>& dims ,
                                          const std::vector<double>& coord ,
                                          const std::vector<double>& zcorn ,
                                          const int * actnum,
                                          const double * mapaxes)


    {
        size_t nCells=dims[0]*dims[1]*dims[2];

        m_coord = coord;
        m_zcorn = zcorn;

        ZcornMapper mapper( getNX(), getNY(), getNZ());
        zcorn_fixed = mapper.fixupZCORN( m_zcorn );

        m_actnum.clear();

        if (actnum != nullptr){
            m_actnum.resize(nCells);

            for (size_t n = 0; n < nCells; n++){
                m_actnum[n] = actnum[n];
            }

        } else {
            m_actnum.assign(nCells, 1);
        }

        if (mapaxes != nullptr){
            m_mapaxes.resize(6);
            for (size_t n = 0; n < 6; n++){
                m_mapaxes[n] = mapaxes[n];
            }
        }
    }

    void EclipseGrid::initCornerPointGrid(const std::array<int,3>& dims, const Deck& deck) {
        assertCornerPointKeywords( dims , deck);
        {
            const auto& ZCORNKeyWord = deck.getKeyword<ParserKeywords::ZCORN>();
            const auto& COORDKeyWord = deck.getKeyword<ParserKeywords::COORD>();

            const std::vector<double>& zcorn = ZCORNKeyWord.getSIDoubleData();
            const std::vector<double>& coord = COORDKeyWord.getSIDoubleData();

            int * actnum = nullptr;
            std::vector<int> actnumVector;

            if (deck.hasKeyword<ParserKeywords::ACTNUM>()) {
                const auto& actnumKeyword = deck.getKeyword<ParserKeywords::ACTNUM>();
                actnumVector = actnumKeyword.getIntData();

                const auto nGlobCells = dims[0] * dims[1] * dims[2];
                if (actnumVector.size() == static_cast<decltype(actnumVector.size())>(nGlobCells)) {
                    actnum=actnumVector.data();
                }
             }

            initCornerPointGrid( dims, coord , zcorn, actnum, nullptr );
        }
    }

    bool EclipseGrid::hasCornerPointKeywords(const Deck& deck) {
        if (deck.hasKeyword<ParserKeywords::ZCORN>() && deck.hasKeyword<ParserKeywords::COORD>())
            return true;
        else
            return false;
    }


    void EclipseGrid::assertCornerPointKeywords(const std::array<int, 3>& dims , const Deck& deck)
    {
        const int nx = dims[0];
        const int ny = dims[1];
        const int nz = dims[2];
        {
            const auto& ZCORNKeyWord = deck.getKeyword<ParserKeywords::ZCORN>();

            if (ZCORNKeyWord.getDataSize() != static_cast<size_t>(8*nx*ny*nz)) {
                const std::string msg =
                    "Wrong size of the ZCORN keyword: Expected 8*x*ny*nz = "
                    + std::to_string(static_cast<long long>(8*nx*ny*nz)) + " is "
                    + std::to_string(static_cast<long long>(ZCORNKeyWord.getDataSize()));
                OpmLog::error(msg);
                throw std::invalid_argument(msg);
            }
        }

        {
            const auto& COORDKeyWord = deck.getKeyword<ParserKeywords::COORD>();
            if (COORDKeyWord.getDataSize() != static_cast<size_t>(6*(nx + 1)*(ny + 1))) {
                const std::string msg =
                    "Wrong size of the COORD keyword: Expected 6*(nx + 1)*(ny + 1) = "
                    + std::to_string(static_cast<long long>(6*(nx + 1)*(ny + 1))) + " is "
                    + std::to_string(static_cast<long long>(COORDKeyWord.getDataSize()));
                OpmLog::error(msg);
                throw std::invalid_argument(msg);
            }
        }
    }


    bool EclipseGrid::hasGDFILE(const Deck& deck) {
        return deck.hasKeyword<ParserKeywords::GDFILE>();
    }

    bool EclipseGrid::hasCartesianKeywords(const Deck& deck) {
        if (hasDVDEPTHZKeywords( deck ))
            return true;
        else
            return hasDTOPSKeywords(deck);
    }

    bool EclipseGrid::hasCylindricalKeywords(const Deck& deck) {
        if (deck.hasKeyword<ParserKeywords::INRAD>() &&
            deck.hasKeyword<ParserKeywords::TOPS>() &&
            (deck.hasKeyword<ParserKeywords::DZ>() || deck.hasKeyword<ParserKeywords::DZV>()) &&
            (deck.hasKeyword<ParserKeywords::DRV>() || deck.hasKeyword<ParserKeywords::DR>()) &&
            (deck.hasKeyword<ParserKeywords::DTHETA>() || deck.hasKeyword<ParserKeywords::DTHETAV>()))
            return true;
        else
            return false;
    }


    bool EclipseGrid::hasDVDEPTHZKeywords(const Deck& deck) {
        if (deck.hasKeyword<ParserKeywords::DXV>() &&
            deck.hasKeyword<ParserKeywords::DYV>() &&
            deck.hasKeyword<ParserKeywords::DZV>() &&
            deck.hasKeyword<ParserKeywords::DEPTHZ>())
            return true;
        else
            return false;
    }

    bool EclipseGrid::hasDTOPSKeywords(const Deck& deck) {
        if ((deck.hasKeyword<ParserKeywords::DX>() || deck.hasKeyword<ParserKeywords::DXV>()) &&
            (deck.hasKeyword<ParserKeywords::DY>() || deck.hasKeyword<ParserKeywords::DYV>()) &&
            (deck.hasKeyword<ParserKeywords::DZ>() || deck.hasKeyword<ParserKeywords::DZV>()) &&
            deck.hasKeyword<ParserKeywords::TOPS>())
            return true;
        else
            return false;
    }

    void EclipseGrid::assertVectorSize(const std::vector<double>& vector , size_t expectedSize , const std::string& vectorName) {
        if (vector.size() != expectedSize)
            throw std::invalid_argument("Wrong size for keyword: " + vectorName + ". Expected: " + std::to_string(expectedSize) + " got: " + std::to_string(vector.size()));
    }


    /*
      The body of the for loop in this method looks slightly
      peculiar. The situation is as follows:

        1. The EclipseGrid class will assemble the necessary keywords
           and create an ert ecl_grid instance.

        2. The ecl_grid instance will export ZCORN, COORD and ACTNUM
           data which will be used by the UnstructureGrid constructor
           in opm-core. If the ecl_grid is created with ZCORN as an
           input keyword that data is retained in the ecl_grid
           structure, otherwise the ZCORN data is created based on the
           internal cell geometries.

        3. When constructing the UnstructuredGrid structure strict
           numerical comparisons of ZCORN values are used to detect
           cells in contact, if all the elements the elements in the
           TOPS vector are specified[1] we will typically not get
           bitwise equality between the bottom of one cell and the top
           of the next.

       To remedy this we enforce bitwise equality with the
       construction:

          if (std::abs(nextValue - TOPS[targetIndex]) < z_tolerance)
             TOPS[targetIndex] = nextValue;

       [1]: This is of course assuming the intention is to construct a
            fully connected space covering grid - if that is indeed
            not the case the barriers must be thicker than 1e-6m to be
            retained.
    */

    std::vector<double> EclipseGrid::createTOPSVector(const std::array<int, 3>& dims,
            const std::vector<double>& DZ, const Deck& deck)
    {
        double z_tolerance = 1e-6;
        size_t volume = dims[0] * dims[1] * dims[2];
        size_t area = dims[0] * dims[1];
        const auto& TOPSKeyWord = deck.getKeyword<ParserKeywords::TOPS>();
        std::vector<double> TOPS = TOPSKeyWord.getSIDoubleData();

        if (TOPS.size() >= area) {
            size_t initialTOPSize = TOPS.size();
            TOPS.resize( volume );

            for (size_t targetIndex = area; targetIndex < volume; targetIndex++) {
                size_t sourceIndex = targetIndex - area;
                double nextValue = TOPS[sourceIndex] + DZ[sourceIndex];

                if (targetIndex >= initialTOPSize)
                    TOPS[targetIndex] = nextValue;
                else {
                    if (std::abs(nextValue - TOPS[targetIndex]) < z_tolerance)
                        TOPS[targetIndex] = nextValue;
                }

            }
        }

        if (TOPS.size() != volume)
            throw std::invalid_argument("TOPS size mismatch");

        return TOPS;
    }

    std::vector<double> EclipseGrid::createDVector(const std::array<int, 3>& dims, size_t dim, const std::string& DKey,
            const std::string& DVKey, const Deck& deck)
    {
        size_t volume = dims[0] * dims[1] * dims[2];
        size_t area = dims[0] * dims[1];
        std::vector<double> D;
        if (deck.hasKeyword(DKey)) {
            D = deck.getKeyword( DKey ).getSIDoubleData();


            if (D.size() >= area && D.size() < volume) {
                /*
                  Only the top layer is required; for layers below the
                  top layer the value from the layer above is used.
                */
                size_t initialDSize = D.size();
                D.resize( volume );
                for (size_t targetIndex = initialDSize; targetIndex < volume; targetIndex++) {
                    size_t sourceIndex = targetIndex - area;
                    D[targetIndex] = D[sourceIndex];
                }
            }

            if (D.size() != volume)
                throw std::invalid_argument(DKey + " size mismatch");
        } else {
            const auto& DVKeyWord = deck.getKeyword(DVKey);
            const std::vector<double>& DV = DVKeyWord.getSIDoubleData();
            if (DV.size() != (size_t) dims[dim])
                throw std::invalid_argument(DVKey + " size mismatch");
            D.resize( volume );
            scatterDim( dims , dim , DV , D );
        }
        return D;
    }


    void EclipseGrid::scatterDim(const std::array<int, 3>& dims , size_t dim , const std::vector<double>& DV , std::vector<double>& D) {
        int index[3];
        for (index[2] = 0;  index[2] < dims[2]; index[2]++) {
            for (index[1] = 0; index[1] < dims[1]; index[1]++) {
                for (index[0] = 0;  index[0] < dims[0]; index[0]++) {
                    size_t globalIndex = index[2] * dims[1] * dims[0] + index[1] * dims[0] + index[0];
                    D[globalIndex] = DV[ index[dim] ];
                }
            }
        }
    }

    bool EclipseGrid::equal(const EclipseGrid& other) const {

        //double reltol = 1.0e-6;

        if (m_coord.size() != other.m_coord.size())
            return false;

        if (m_zcorn.size() != other.m_zcorn.size())
            return false;

        if (m_actnum.size() != other.m_actnum.size())
            return false;

        if (m_mapaxes.size() != other.m_mapaxes.size())
            return false;

        if (m_actnum != other.m_actnum)
            return false;

        if (m_coord != other.m_coord)
            return false;

        if (m_zcorn != other.m_zcorn)
            return false;

        if (m_mapaxes != other.m_mapaxes)
            return false;

        bool status = (m_pinch.equal( other.m_pinch )  && (m_minpvMode == other.getMinpvMode()));

        if(m_minpvMode!=MinpvMode::ModeEnum::Inactive) {
            status = status && (m_minpvVector == other.getMinpvVector());
        }

        return status;
    }

    size_t EclipseGrid::getNumActive( ) const {
        return m_nactive;
    }

    bool EclipseGrid::allActive( ) const {
        return (getNumActive() == getCartesianSize());
    }

    bool EclipseGrid::cellActive( size_t globalIndex ) const {
        assertGlobalIndex( globalIndex );

        return m_actnum[globalIndex]>0;
    }

    bool EclipseGrid::cellActive( size_t i , size_t j , size_t k ) const {
        assertIJK(i,j,k);

        size_t globalIndex = getGlobalIndex(i,j,k);

        return m_actnum[globalIndex]>0;
    }


    double EclipseGrid::getCellVolume(size_t globalIndex) const {

        assertGlobalIndex( globalIndex );

        return m_volume[globalIndex];
    }

    double EclipseGrid::getCellVolume(size_t i , size_t j , size_t k) const {

        assertIJK(i,j,k);

        size_t globalIndex = getGlobalIndex( i,j,k );

        return getCellVolume(globalIndex);
    }

    double EclipseGrid::getCellThickness(size_t i , size_t j , size_t k) const {
        assertIJK(i,j,k);

        const std::array<int, 3> dims = getNXYZ();
        size_t globalIndex = i + j*dims[0] + k*dims[0]*dims[1];

        return getCellThickness(globalIndex);
    }

    double EclipseGrid::getCellThickness(size_t globalIndex) const {
        assertGlobalIndex( globalIndex );

        return m_dz[globalIndex];
    }

    std::array<double, 3> EclipseGrid::getCellDims(size_t globalIndex) const {
        assertGlobalIndex( globalIndex );

        return std::array<double,3> {{m_dx[globalIndex] , m_dy[globalIndex] , m_dz[globalIndex] }};
    }

    std::array<double, 3> EclipseGrid::getCellDims(size_t i , size_t j , size_t k) const {
        assertIJK(i,j,k);
        {
            size_t globalIndex = getGlobalIndex( i,j,k );

            return getCellDims(globalIndex);
        }
    }

    const std::array<double, 3>& EclipseGrid::getCellCenter(size_t globalIndex) const {
        assertGlobalIndex( globalIndex );

        return m_cellCenter[globalIndex];
    }

    const std::array<double, 3>& EclipseGrid::getCellCenter(size_t i,size_t j, size_t k) const {
        assertIJK(i,j,k);

        const std::array<int, 3> dims = getNXYZ();

        size_t globalIndex = i + j*dims[0] + k*dims[0]*dims[1];

        return getCellCenter(globalIndex);
    }

    /*
      This is the numbering of the corners in the cell.

       bottom                           j
         6---7                        /|\
         |   |                         |
         4---5                         |
                                       |
       top                             o---------->  i
         2---3
         |   |
         0---1

     */

    std::array<double, 3> EclipseGrid::getCornerPos(size_t i,size_t j, size_t k, size_t corner_index) const {
        assertIJK(i,j,k);
        if (corner_index >= 8)
            throw std::invalid_argument("Invalid corner position");
        {
            const std::array<int, 3> dims = getNXYZ();

            std::array<double,8> X = {0.0};
            std::array<double,8> Y = {0.0};
            std::array<double,8> Z = {0.0};

            std::array<int, 3> ijk;

            ijk[0] = i;
            ijk[1] = j;
            ijk[2] = k;

            getCellCorners(ijk, dims, X, Y, Z );

            return std::array<double, 3> {{X[corner_index], Y[corner_index], Z[corner_index]}};
        }
    }

    double EclipseGrid::getCellDepth(size_t globalIndex) const {
        assertGlobalIndex( globalIndex );

        return m_depth[globalIndex];
    }

    double EclipseGrid::getCellDepth(size_t i, size_t j, size_t k) const {
        assertIJK(i,j,k);

        size_t globalIndex = getGlobalIndex( i,j,k );

        return getCellDepth(globalIndex);
    }

    const std::vector<int>& EclipseGrid::getACTNUM( ) const {

        return m_actnum;
    }

    const std::vector<double>& EclipseGrid::getMAPAXES( ) const {

        return m_mapaxes;
    }

    const std::vector<double>& EclipseGrid::getCOORD() const {

        return m_coord;
    }

    size_t EclipseGrid::fixupZCORN() {

        ZcornMapper mapper( getNX(), getNY(), getNZ());

        return mapper.fixupZCORN( m_zcorn );
    }

    const std::vector<double>& EclipseGrid::getZCORN( ) const {

        return m_zcorn;
    }

    void EclipseGrid::save(const std::string& filename, bool formatted, const Opm::NNC& nnc, const Opm::UnitSystem& units) const {

        Opm::UnitSystem::UnitType unitSystemType = units.getType();
        const auto length = ::Opm::UnitSystem::measure::length;

        const std::array<int, 3> dims = getNXYZ();

        // Preparing vectors to be saved

        // create coord vector of floats with input units, converted from SI
        std::vector<float> coord_f;
        coord_f.resize(m_coord.size());

        for (size_t n=0; n< m_coord.size(); n++){
            coord_f[n] = static_cast<double>(units.from_si(length, m_coord[n]));
        }

        // create zcorn vector of floats with input units, converted from SI
        std::vector<float> zcorn_f;
        zcorn_f.resize(m_zcorn.size());

        for (size_t n=0; n< m_zcorn.size(); n++){
            zcorn_f[n] = static_cast<double>(units.from_si(length, m_zcorn[n]));
        }

        std::vector<float> mapaxes_f;

        std::vector<int> filehead(100,0);
        filehead[0] = 3;                     // version number
        filehead[1] = 2007;                  // release year
        filehead[6] = 1;                     // corner point grid

        std::vector<int> gridhead(100,0);
        gridhead[0] = 1;                    // corner point grid
        gridhead[1] = dims[0];              // nI
        gridhead[2] = dims[1];              // nJ
        gridhead[3] = dims[2];              // nK
        gridhead[24] = 1;                   // corner point grid

        std::vector<int> nnchead(10, 0);
        std::vector<int> nnc1;
        std::vector<int> nnc2;

        for (const NNCdata& n : nnc.nncdata() ) {
            nnc1.push_back(n.cell1 + 1);
            nnc2.push_back(n.cell2 + 1);
        }

        nnchead[0] = nnc1.size();

        std::vector<std::string> gridunits;

        switch (unitSystemType) {
        case Opm::UnitSystem::UnitType::UNIT_TYPE_METRIC:
            gridunits.push_back("METRES");
            break;
        case Opm::UnitSystem::UnitType::UNIT_TYPE_FIELD:
            gridunits.push_back("FEET");
            break;
        case Opm::UnitSystem::UnitType::UNIT_TYPE_LAB:
            gridunits.push_back("CM");
            break;
        default:
            OPM_THROW(std::runtime_error, "Unit system not supported when writing to EGRID file");
            break;
        }

        gridunits.push_back("");

        // map units is not dependent on deck units. A user may specify FIELD units for the model
        // and metric units for the MAPAXES keyword (MAPUNITS)

        std::vector<std::string> mapunits;

        if ((m_mapunits.size() > 0) && (m_mapaxes.size() > 0)) {
            mapunits.push_back(m_mapunits);
        }

        if (m_mapaxes.size() > 0){
            for (double dv :  m_mapaxes){
                mapaxes_f.push_back(static_cast<float>(dv));
            }
        }

        std::vector<int> endgrid = {};

        // Writing vectors to egrid file

        Opm::EclIO::EclOutput egridfile(filename, formatted);
        egridfile.write("FILEHEAD", filehead);

        if (mapunits.size() > 0) {
            egridfile.write("MAPUNITS", mapunits);
        }

        if (mapaxes_f.size() > 0){
            egridfile.write("MAPAXES", mapaxes_f);
        }

        egridfile.write("GRIDUNIT", gridunits);
        egridfile.write("GRIDHEAD", gridhead);

        egridfile.write("COORD", coord_f);
        egridfile.write("ZCORN", zcorn_f);

        egridfile.write("ACTNUM", m_actnum);
        egridfile.write("ENDGRID", endgrid);

        if (nnc1.size() > 0){
            egridfile.write("NNCHEAD", nnchead);
            egridfile.write("NNC1", nnc1);
            egridfile.write("NNC2", nnc2);
        }
    }

    const std::vector<int>& EclipseGrid::getActiveMap() const {

        return m_active_to_global;
    }

    void EclipseGrid::resetACTNUM() {

        const std::array<int, 3> dims = getNXYZ();
        m_nactive = dims[0]*dims[1]*dims[2];

        std::vector<int> all_active(m_nactive, 1);

        m_actnum.clear();
        m_actnum = all_active;

        m_global_to_active = all_active;
        std::iota(m_global_to_active.begin(), m_global_to_active.end(), 0);

        m_active_to_global = m_global_to_active;
    }

    void EclipseGrid::resetACTNUM( const std::vector<int>& actnum) {

        if (actnum.size() != getCartesianSize()) {
            throw std::runtime_error("resetACTNUM(): actnum vector size differs from logical cartesian size of grid.");
        }

        m_actnum = actnum;
        m_global_to_active.clear();
        m_active_to_global.clear();
        m_global_to_active.reserve(actnum.size());
        m_nactive = 0;

        for (size_t n = 0; n < actnum.size() ; n++) {
            if (actnum[n] > 0) {
                m_global_to_active.push_back(m_nactive);
                m_active_to_global.push_back(n);
                ++m_nactive;
            } else {
                m_global_to_active.push_back(-1);
            }
        }
    }

    ZcornMapper EclipseGrid::zcornMapper() const {
        return ZcornMapper( getNX() , getNY(), getNZ() );
    }

    ZcornMapper::ZcornMapper(size_t nx , size_t ny, size_t nz)
        : dims( {{nx,ny,nz}} ),
          stride( {{2 , 4*nx, 8*nx*ny}} ),
          cell_shift( {{0 , 1 , 2*nx , 2*nx + 1 , 4*nx*ny , 4*nx*ny + 1, 4*nx*ny + 2*nx , 4*nx*ny + 2*nx + 1 }})
    {
    }


    /* lower layer:   upper layer  (higher value of z - i.e. lower down in resrvoir).

         2---3           6---7
         |   |           |   |
         0---1           4---5
    */

    size_t ZcornMapper::index(size_t i, size_t j, size_t k, int c) const {
        if ((i >= dims[0]) || (j >= dims[1]) || (k >= dims[2]) || (c < 0) || (c >= 8))
            throw std::invalid_argument("Invalid cell argument");

        return i*stride[0] + j*stride[1] + k*stride[2] + cell_shift[c];
    }

    size_t ZcornMapper::size() const {
        return dims[0] * dims[1] * dims[2] * 8;
    }

    size_t ZcornMapper::index(size_t g, int c) const {
        int k = g / (dims[0] * dims[1]);
        g -= k * dims[0] * dims[1];

        int j = g / dims[0];
        g -= j * dims[0];

        int i = g;

        return index(i,j,k,c);
    }

    bool ZcornMapper::validZCORN( const std::vector<double>& zcorn) const {
        int sign = zcorn[ this->index(0,0,0,0) ] <= zcorn[this->index(0,0, this->dims[2] - 1,4)] ? 1 : -1;
        for (size_t j=0; j < this->dims[1]; j++)
            for (size_t i=0; i < this->dims[0]; i++)
                for (size_t c=0; c < 4; c++)
                    for (size_t k=0; k < this->dims[2]; k++) {
                        /* Between cells */
                        if (k > 0) {
                            size_t index1 = this->index(i,j,k-1,c+4);
                            size_t index2 = this->index(i,j,k,c);
                            if ((zcorn[index2] - zcorn[index1]) * sign < 0)
                                return false;
                        }

                        /* In cell */
                        {
                            size_t index1 = this->index(i,j,k,c);
                            size_t index2 = this->index(i,j,k,c+4);
                            if ((zcorn[index2] - zcorn[index1]) * sign < 0)
                                return false;
                        }
                    }

        return true;
    }


    size_t ZcornMapper::fixupZCORN( std::vector<double>& zcorn) {
        int sign = zcorn[ this->index(0,0,0,0) ] <= zcorn[this->index(0,0, this->dims[2] - 1,4)] ? 1 : -1;
        size_t cells_adjusted = 0;

        for (size_t k=0; k < this->dims[2]; k++)
            for (size_t j=0; j < this->dims[1]; j++)
                for (size_t i=0; i < this->dims[0]; i++)
                    for (size_t c=0; c < 4; c++) {
                        /* Cell to cell */
                        if (k > 0) {
                            size_t index1 = this->index(i,j,k-1,c+4);
                            size_t index2 = this->index(i,j,k,c);

                            if ((zcorn[index2] - zcorn[index1]) * sign < 0 ) {
                                zcorn[index2] = zcorn[index1];
                                cells_adjusted++;
                            }
                        }

                        /* Cell internal */
                        {
                            size_t index1 = this->index(i,j,k,c);
                            size_t index2 = this->index(i,j,k,c+4);

                            if ((zcorn[index2] - zcorn[index1]) * sign < 0 ) {
                                zcorn[index2] = zcorn[index1];
                                cells_adjusted++;
                            }
                        }
                    }
        return cells_adjusted;
    }

    CoordMapper::CoordMapper(size_t nx_, size_t ny_) :
        nx(nx_),
        ny(ny_)
    {
    }

    size_t CoordMapper::size() const {
        return (this->nx + 1) * (this->ny + 1) * 6;
    }


    size_t CoordMapper::index(size_t i, size_t j, size_t dim, size_t layer) const {
        if (i > this->nx)
            throw std::invalid_argument("Out of range");

        if (j > this->ny)
            throw std::invalid_argument("Out of range");

        if (dim > 2)
            throw std::invalid_argument("Out of range");

        if (layer > 1)
            throw std::invalid_argument("Out of range");

        return 6*( i + j*(this->nx + 1) ) +  layer * 3 + dim;
    }
}



