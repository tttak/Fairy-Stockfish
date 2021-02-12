/*
  Fairy-Stockfish, a UCI chess variant playing engine derived from Stockfish
  Copyright (C) 2018-2021 Fabian Fichter

  Fairy-Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Fairy-Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>

#include "parser.h"
#include "piece.h"
#include "variant.h"

using std::string;

VariantMap variants; // Global object

namespace {
    // Define variant rules
    Variant* fairy_variant_base() {
        Variant* v = new Variant();
        v->pieceToCharTable = "PNBRQ................Kpnbrq................k";
        v->endgameEval = false;
        return v;
    }
    Variant* chess_variant() {
        Variant* v = fairy_variant_base();
        v->endgameEval = true;
        return v;
    }
    Variant* fairy_variant() {
        Variant* v = chess_variant();
        v->add_piece(SILVER, 's');
        v->add_piece(FERS, 'f');
        return v;
    }
    Variant* minishogi_variant_base() {
        Variant* v = fairy_variant_base();
        v->variantTemplate = "shogi";
        v->maxRank = RANK_5;
        v->maxFile = FILE_E;
        v->reset_pieces();
        v->add_piece(SHOGI_PAWN, 'p');
        v->add_piece(SILVER, 's');
        v->add_piece(GOLD, 'g');
        v->add_piece(BISHOP, 'b');
        v->add_piece(DRAGON_HORSE, 'h');
        v->add_piece(ROOK, 'r');
        v->add_piece(DRAGON, 'd');
        v->add_piece(KING, 'k');
        v->startFen = "rbsgk/4p/5/P4/KGSBR[-] w 0 1";
        v->pieceDrops = true;
        v->capturesToHand = true;
        v->promotionRank = RANK_5;
        v->promotionPieceTypes = {};
        v->doubleStep = false;
        v->castling = false;
        v->promotedPieceType[SHOGI_PAWN] = GOLD;
        v->promotedPieceType[SILVER]     = GOLD;
        v->promotedPieceType[BISHOP]     = DRAGON_HORSE;
        v->promotedPieceType[ROOK]       = DRAGON;
        v->shogiDoubledPawn = false;
        v->immobilityIllegal = true;
        v->shogiPawnDropMateIllegal = true;
        v->stalemateValue = -VALUE_MATE;
        v->nFoldRule = 4;
        v->nMoveRule = 0;
        v->perpetualCheckIllegal = true;
        return v;
    }
    Variant* minishogi_variant() {
        Variant* v = minishogi_variant_base();
        v->pieceToCharTable = "P.BR.S...G.+.++.+Kp.br.s...g.+.++.+k";
        v->pocketSize = 5;
        v->nFoldValue = -VALUE_MATE;
        v->nFoldValueAbsolute = true;
        return v;
    }

} // namespace


/// VariantMap::init() is called at startup to initialize all predefined variants

void VariantMap::init() {
    // Add to UCI_Variant option
    add("fairy", fairy_variant()->conclude()); // fairy variant used for endgame code initialization
    add("minishogi", minishogi_variant()->conclude());
    add("mini", minishogi_variant()->conclude());
}


/// VariantMap::parse_istream reads variants from an INI-style configuration input stream.

template <bool DoCheck>
void VariantMap::parse_istream(std::istream& file) {
    std::string variant, variant_template, key, value, input;
    while (file.peek() != '[' && std::getline(file, input)) {}

    std::vector<std::string> varsToErase = {};
    while (file.get() && std::getline(std::getline(file, variant, ']'), input))
    {
        // Extract variant template, if specified
        if (!std::getline(std::getline(std::stringstream(variant), variant, ':'), variant_template))
            variant_template = "";

        // Read variant rules
        Config attribs = {};
        while (file.peek() != '[' && std::getline(file, input))
        {
            std::stringstream ss(input);
            if (ss.peek() != '#' && std::getline(std::getline(ss, key, '=') >> std::ws, value) && !key.empty())
                attribs[key.erase(key.find_last_not_of(" ") + 1)] = value;
        }

        // Create variant
        if (variants.find(variant) != variants.end())
            std::cerr << "Variant '" << variant << "' already exists." << std::endl;
        else if (!variant_template.empty() && variants.find(variant_template) == variants.end())
            std::cerr << "Variant template '" << variant_template << "' does not exist." << std::endl;
        else
        {
            if (DoCheck)
                std::cerr << "Parsing variant: " << variant << std::endl;
            Variant* v = !variant_template.empty() ? VariantParser<DoCheck>(attribs).parse(new Variant(*variants.find(variant_template)->second))
                                                   : VariantParser<DoCheck>(attribs).parse();
            if (v->maxFile <= FILE_MAX && v->maxRank <= RANK_MAX)
            {
                add(variant, v->conclude());
                // In order to allow inheritance, we need to temporarily add configured variants
                // even when only checking them, but we remove them later after parsing is finished.
                if (DoCheck)
                    varsToErase.push_back(variant);
            }
            else
                delete v;
        }
    }
    // Clean up temporary variants
    for (std::string tempVar : varsToErase)
    {
        delete variants[tempVar];
        variants.erase(tempVar);
    }
}

/// VariantMap::parse reads variants from an INI-style configuration file.

template <bool DoCheck>
void VariantMap::parse(std::string path) {
    if (path.empty() || path == "<empty>")
        return;
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Unable to open file " << path << std::endl;
        return;
    }
    parse_istream<DoCheck>(file);
    file.close();
}

template void VariantMap::parse<true>(std::string path);
template void VariantMap::parse<false>(std::string path);

void VariantMap::add(std::string s, const Variant* v) {
  insert(std::pair<std::string, const Variant*>(s, v));
}

void VariantMap::clear_all() {
  for (auto const& element : *this)
      delete element.second;
  clear();
}

std::vector<std::string> VariantMap::get_keys() {
  std::vector<std::string> keys;
  for (auto const& element : *this)
      keys.push_back(element.first);
  return keys;
}
