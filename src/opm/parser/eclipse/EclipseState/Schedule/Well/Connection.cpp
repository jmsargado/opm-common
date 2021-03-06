/*
  Copyright 2013 Statoil ASA.

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

#include <algorithm>
#include <cassert>
#include <vector>
#include <sstream>
#include <iostream>

#include <opm/parser/eclipse/Deck/DeckItem.hpp>
#include <opm/parser/eclipse/Deck/DeckKeyword.hpp>
#include <opm/parser/eclipse/Deck/DeckRecord.hpp>
#include <opm/parser/eclipse/Parser/ParseContext.hpp>
#include <opm/parser/eclipse/EclipseState/Eclipse3DProperties.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/EclipseGrid.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Well/Connection.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Schedule.hpp>
#include <opm/parser/eclipse/EclipseState/Util/Value.hpp>

namespace Opm {


    Connection::Connection(int i, int j , int k ,
                           int compnum,
                           double depth,
                           State stateArg ,
                           double CF,
                           double Kh,
                           double rw,
                           double r0,
                           double skin_factor,
                           const int satTableId,
                           const Direction directionArg,
			   const std::size_t seqIndex,
			   const double segDistStart,
			   const double segDistEnd,
			   const bool defaultSatTabId)
        : direction(directionArg),
          center_depth(depth),
          open_state(stateArg),
          sat_tableId(satTableId),
          m_complnum( compnum ),
          m_CF(CF),
          m_Kh(Kh),
          m_rw(rw),
          m_r0(r0),
          m_skin_factor(skin_factor),
          ijk({i,j,k}),
          m_seqIndex(seqIndex),
          m_segDistStart(segDistStart),
          m_segDistEnd(segDistEnd),
          m_defaultSatTabId(defaultSatTabId)
    {}


    bool Connection::sameCoordinate(const int i, const int j, const int k) const {
        if ((ijk[0] == i) && (ijk[1] == j) && (ijk[2] == k)) {
            return true;
        } else {
            return false;
        }
    }

    int Connection::getI() const {
        return ijk[0];
    }

    int Connection::getJ() const {
        return ijk[1];
    }

    int Connection::getK() const {
        return ijk[2];
    }

    bool Connection::attachedToSegment() const {
        return (segment_number > 0);
    }

    const std::size_t& Connection::getSeqIndex() const {
        return m_seqIndex;
    }

    const bool& Connection::getDefaultSatTabId() const {
        return m_defaultSatTabId;
    }

    const std::size_t& Connection::getCompSegSeqIndex() const {
        return m_compSeg_seqIndex;
    }

    Connection::Direction Connection::dir() const {
        return this->direction;
    }

    const double& Connection::getSegDistStart() const {
        return m_segDistStart;
    }

    const double& Connection::getSegDistEnd() const {
        return m_segDistEnd;
    }


    void Connection::setCompSegSeqIndex(std::size_t index) {
        m_compSeg_seqIndex = index;
    }

    void Connection::setDefaultSatTabId(bool id) {
        m_defaultSatTabId = id;
    }

    void Connection::setSegDistStart(const double& distStart) {
        m_segDistStart = distStart;
    }

    void Connection::setSegDistEnd(const double& distEnd) {
        m_segDistEnd = distEnd;
    }

    double Connection::depth() const {
        return this->center_depth;
    }

    Connection::State Connection::state() const {
        return this->open_state;
    }

    int Connection::satTableId() const {
        return this->sat_tableId;
    }

    int Connection::complnum() const {
        return this->m_complnum;
    }

    void Connection::setComplnum(int complnum) {
        this->m_complnum = complnum;
    }

    double Connection::CF() const {
        return this->m_CF;
    }

    double Connection::Kh() const {
        return this->m_Kh;
    }

    double Connection::rw() const {
        return this->m_rw;
    }

    double Connection::r0() const {
        return this->m_r0;
    }

    double Connection::skinFactor() const {
        return this->m_skin_factor;
    }

    void Connection::setState(State state) {
        this->open_state = state;
    }

    void Connection::updateSegment(int segment_number_arg, double center_depth_arg, std::size_t seqIndex) {
        this->segment_number = segment_number_arg;
        this->center_depth = center_depth_arg;
        this->m_seqIndex = seqIndex;
    }

    int Connection::segment() const {
        return this->segment_number;
    }

    void Connection::scaleWellPi(double wellPi) {
        this->wPi *= wellPi;
    }

    double Connection::wellPi() const {
        return this->wPi;
    }



    std::string Connection::str() const {
        std::stringstream ss;
        ss << "ijk: " << this->ijk[0] << ","  << this->ijk[1] << "," << this->ijk[2] << std::endl;
        ss << "COMPLNUM " << this->m_complnum << std::endl;
        ss << "CF " << this->m_CF << std::endl;
        ss << "RW " << this->m_rw << std::endl;
        ss << "R0 " << this->m_r0 << std::endl;
        ss << "skinf " << this->m_skin_factor << std::endl;
        ss << "wPi " << this->wPi << std::endl;
        ss << "kh " << this->m_Kh << std::endl;
        ss << "sat_tableId " << this->sat_tableId << std::endl;
        ss << "open_state " << Connection::State2String(this->open_state) << std::endl;
        ss << "direction " << Connection::Direction2String(this->direction) << std::endl;
        ss << "segment_nr " << this->segment_number << std::endl;
        ss << "center_depth " << this->center_depth << std::endl;
        ss << "seqIndex " << this->m_seqIndex << std::endl;

        return ss.str();
}

    bool Connection::operator==( const Connection& rhs ) const {
        bool eq = this->ijk == rhs.ijk
            && this->m_complnum == rhs.m_complnum
            && this->m_CF == rhs.m_CF
            && this->m_rw == rhs.m_rw
            && this->m_r0 == rhs.m_r0
            && this->m_skin_factor == rhs.m_skin_factor
            && this->wPi == rhs.wPi
            && this->m_Kh == rhs.m_Kh
            && this->sat_tableId == rhs.sat_tableId
            && this->open_state == rhs.open_state
            && this->direction == rhs.direction
            && this->segment_number == rhs.segment_number
            && this->center_depth == rhs.center_depth
            && this->m_seqIndex == rhs.m_seqIndex;
        if (!eq) {
            //std::cout << this->str() << rhs.str() << std::endl;
        }
        return eq;
    }

    bool Connection::operator!=( const Connection& rhs ) const {
        return !( *this == rhs );
    }



const std::string Connection::State2String( State enumValue ) {
    switch( enumValue ) {
    case State::OPEN:
        return "OPEN";
    case State::AUTO:
        return "AUTO";
    case State::SHUT:
        return "SHUT";
    default:
        throw std::invalid_argument("Unhandled enum value");
    }
}


Connection::State Connection::StateFromString( const std::string& stringValue ) {
    if (stringValue == "OPEN")
        return State::OPEN;
    else if (stringValue == "SHUT")
        return State::SHUT;
    else if (stringValue == "STOP")
        return State::SHUT;
    else if (stringValue == "AUTO")
        return State::AUTO;
    else
        throw std::invalid_argument("Unknown enum state string: " + stringValue );
}


std::string Connection::Direction2String(const Direction enumValue)
{
    std::string stringValue;

    switch (enumValue) {
    case Direction::X:
        stringValue = "X";
        break;

    case Direction::Y:
        stringValue = "Y";
        break;

    case Direction::Z:
        stringValue = "Z";
        break;
    }

    return stringValue;
}


Connection::Direction Connection::DirectionFromString(const std::string& s )
{
    Direction direction;

    if      (s == "X") { direction = Direction::X; }
    else if (s == "Y") { direction = Direction::Y; }
    else if (s == "Z") { direction = Direction::Z; }
    else {
        std::string msg = "Unsupported completion direction " + s;
        throw std::invalid_argument(msg);
    }

    return direction;
}


const std::string Connection::Order2String( Order enumValue ) {
    switch( enumValue ) {
    case Order::DEPTH:
        return "DEPTH";
    case Order::INPUT:
        return "INPUT";
    case Order::TRACK:
        return "TRACK";
    default:
        throw std::invalid_argument("Unhandled enum value");
    }
}


Connection::Order Connection::OrderFromString(const std::string& stringValue ) {
    if (stringValue == "DEPTH")
        return Order::DEPTH;
    else if (stringValue == "INPUT")
        return Order::INPUT;
    else if (stringValue == "TRACK")
        return Order::TRACK;
    else
        throw std::invalid_argument("Unknown enum state string: " + stringValue );
}



}

