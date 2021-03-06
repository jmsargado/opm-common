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

#include <stdexcept>
#include <iostream>
#include <boost/filesystem.hpp>
#include <cstdio>

#define BOOST_TEST_MODULE CopyRegTests
#include <boost/test/unit_test.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>


#include <opm/parser/eclipse/Parser/Parser.hpp>

#include <opm/parser/eclipse/Deck/Section.hpp>
#include <opm/parser/eclipse/Deck/Deck.hpp>
#include <opm/parser/eclipse/Deck/DeckKeyword.hpp>

#include <opm/parser/eclipse/EclipseState/Eclipse3DProperties.hpp>
#include <opm/parser/eclipse/EclipseState/EclipseState.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/EclipseGrid.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/GridProperty.hpp>
#include <opm/parser/eclipse/EclipseState/Tables/TableManager.hpp>


static Opm::Deck createDeckInvalidArray1() {
    const char *deckData =
        "RUNSPEC\n"
        "\n"
        "DIMENS\n"
        " 10 10 10 /\n"
        "GRID\n"
        "SATNUM\n"
        "  1000*1 /\n"
        "COPYREG\n"
        "  MISSING SATNUM 10 M / \n"
        "/\n"
        "EDIT\n"
        "\n";

    Opm::Parser parser;
    return parser.parseString(deckData) ;
}

static Opm::Deck createDeckInvalidArray2() {
    const char *deckData =
        "RUNSPEC\n"
        "\n"
        "DIMENS\n"
        " 10 10 10 /\n"
        "GRID\n"
        "SATNUM\n"
        "  1000*1 /\n"
        "COPYREG\n"
        "  SATNUM MISSING 10 M / \n"
        "/\n"
        "EDIT\n"
        "\n";

    Opm::Parser parser;
    return parser.parseString(deckData) ;
}


static Opm::Deck createDeckInvalidTypeMismatch() {
    const char *deckData =
        "RUNSPEC\n"
        "\n"
        "DIMENS\n"
        " 10 10 10 /\n"
        "GRID\n"
        "SATNUM\n"
        "  1000*1 /\n"
        "COPYREG\n"
        "  SATNUM PERMX 10 M / \n"
        "/\n"
        "EDIT\n"
        "\n";

    Opm::Parser parser;
    return parser.parseString(deckData) ;
}



static Opm::Deck createDeckInvalidRegion() {
    const char *deckData =
        "RUNSPEC\n"
        "\n"
        "DIMENS\n"
        " 10 10 10 /\n"
        "GRID\n"
        "SATNUM\n"
        "  1000*1 /\n"
        "COPYREG\n"
        "  SATNUM FLUXNUM 10 MX / \n"
        "/\n"
        "EDIT\n"
        "\n";

    Opm::Parser parser;
    return parser.parseString(deckData) ;
}




static Opm::Deck createDeckUnInitialized() {
    const char *deckData =
        "RUNSPEC\n"
        "\n"
        "DIMENS\n"
        " 10 10 10 /\n"
        "GRID\n"
        "REGIONS\n"
        "COPYREG\n"
        "  SATNUM FLUXNUM 10 M / \n"
        "/\n"
        "EDIT\n"
        "\n";

    Opm::Parser parser;
    return parser.parseString(deckData) ;
}


static Opm::Deck createValidIntDeck() {
    const char *deckData =
        "RUNSPEC\n"
        "\n"
        "DIMENS\n"
        " 5 5 1 /\n"
        "GRID\n"
        "DX\n"
        "25*0.25 /\n"
        "DY\n"
        "25*0.25 /\n"
        "DZ\n"
        "25*0.25 /\n"
        "TOPS\n"
        "25*0.25 /\n"
        "MULTNUM \n"
        "1  1  2  2 2\n"
        "1  1  2  2 2\n"
        "1  1  2  2 2\n"
        "1  1  2  2 2\n"
        "1  1  2  2 2\n"
        "/\n"
        "SATNUM\n"
        "  25*10 /\n"
        "FLUXNUM\n"
        "  25*3 /\n"
        "COPYREG\n"
        "  SATNUM FLUXNUM 1    M / \n"
        "/\n"
        "EDIT\n"
        "\n";

    Opm::Parser parser;
    return parser.parseString(deckData) ;
}




BOOST_AUTO_TEST_CASE(InvalidArrayThrows1) {
    Opm::Deck deck = createDeckInvalidArray1();
    BOOST_CHECK_THROW( new Opm::EclipseState(deck) , std::invalid_argument );
}

BOOST_AUTO_TEST_CASE(InvalidArrayThrows2) {
    Opm::Deck deck = createDeckInvalidArray2();
    BOOST_CHECK_THROW( new Opm::EclipseState( deck) , std::invalid_argument );
}



BOOST_AUTO_TEST_CASE(InvalidRegionThrows) {
    Opm::Deck deck = createDeckInvalidRegion();
    BOOST_CHECK_THROW( new Opm::EclipseState( deck) , std::invalid_argument );
}




BOOST_AUTO_TEST_CASE(UnInitializedVectorThrows) {
    Opm::Deck deck = createDeckUnInitialized();
    BOOST_CHECK_THROW( new Opm::EclipseState( deck) , std::invalid_argument );
}


BOOST_AUTO_TEST_CASE(TypeMismatchThrows) {
    Opm::Deck deck = createDeckInvalidTypeMismatch();
    BOOST_CHECK_THROW( new Opm::EclipseState( deck) , std::invalid_argument );
}



BOOST_AUTO_TEST_CASE(IntSetCorrectly) {
    Opm::Deck deck = createValidIntDeck();
    Opm::TableManager tm(deck);
    Opm::EclipseGrid eg(deck);
    Opm::Eclipse3DProperties props(deck, tm, eg);
    const auto& property = props.getIntGridProperty("FLUXNUM").getData();

    for (size_t j = 0; j < 5; j++)
        for (size_t i = 0; i < 5; i++) {
            std::size_t g = eg.getGlobalIndex(i,j,0);
            if (i < 2)
                BOOST_CHECK_EQUAL( 10 , property[g]);
            else
                BOOST_CHECK_EQUAL( 3 , property[g]);
        }
}
