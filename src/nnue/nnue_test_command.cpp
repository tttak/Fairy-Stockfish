#include "../thread.h"
#include "../uci.h"
#include "../variant.h"
#include "evaluate_nnue.h"
#include "nnue_test_command.h"

#include <set>
#include <fstream>

#define ASSERT(X) { \
    if (!(X)) { \
        std::cout \
            << "\nError : ASSERT(" << #X << "), " \
            << __FILE__ << "(" << __LINE__ << "): " \
            << __func__ << std::endl; \
            std::this_thread::sleep_for(std::chrono::microseconds(3000)); \
            *(int*)1 =0; \
    } \
}

namespace Eval {

namespace NNUE {

namespace {

// Testing RawFeatures mainly for incremental computation
void TestFeatures(Position& pos) {
  const std::uint64_t num_games = 1000;
  StateInfo si;
  std::string startfen = variants.find(Options["UCI_Variant"])->second->startFen;

  // TODO Support for other than Shogi
  bool sfen = true;

  pos.set(variants.find(Options["UCI_Variant"])->second, startfen, false, &si, Threads.main(), sfen);
  const int MAX_PLY = 256;

  StateInfo state[MAX_PLY];
  int ply;

  PRNG prng(20171128);

  std::uint64_t num_moves = 0;
  std::vector<std::uint64_t> num_updates(kRefreshTriggers.size() + 1);
  std::vector<std::uint64_t> num_resets(kRefreshTriggers.size());
  constexpr IndexType kUnknown = -1;
  std::vector<IndexType> trigger_map(RawFeatures::kDimensions, kUnknown);

  auto make_index_sets = [&](const Position& pos) {
    std::vector<std::vector<std::set<IndexType>>> index_sets(
        kRefreshTriggers.size(), std::vector<std::set<IndexType>>(2));
    for (IndexType i = 0; i < kRefreshTriggers.size(); ++i) {
      Features::IndexList active_indices[2];

      // TODO Support for non-HalfKP
      //RawFeatures::AppendActiveIndices(pos, kRefreshTriggers[i],
      //                                 active_indices);
      for (const auto perspective : {WHITE, BLACK}) {
        Features::HalfKP<Features::Side::kFriend>::AppendActiveIndices(pos, perspective, &active_indices[perspective]);
      }

      for (const auto perspective : {WHITE, BLACK}) {
        for (const auto index : active_indices[perspective]) {
          ASSERT(index < RawFeatures::kDimensions);
          ASSERT(index_sets[i][perspective].count(index) == 0);
          ASSERT(trigger_map[index] == kUnknown || trigger_map[index] == i);
          index_sets[i][perspective].insert(index);
          trigger_map[index] = i;
        }
      }
    }
    return index_sets;
  };

  auto update_index_sets = [&](const Position& pos, auto* index_sets) {
    for (IndexType i = 0; i < kRefreshTriggers.size(); ++i) {
      Features::IndexList removed_indices[2], added_indices[2];
      bool reset[2];

      // TODO Support for non-HalfKP
      //RawFeatures::AppendChangedIndices(pos, kRefreshTriggers[i],
      //                                  removed_indices, added_indices, reset);
      const auto& dp = pos.state()->dirtyPiece;
      for (const auto perspective : {WHITE, BLACK}) {
        reset[perspective] = (dp.piece[0] == make_piece(perspective, KING));
        if (reset[perspective]) {
          Features::HalfKP<Features::Side::kFriend>::AppendActiveIndices(pos, perspective, &added_indices[perspective]);
        } else {
          Features::HalfKP<Features::Side::kFriend>::AppendChangedIndices(pos, dp, perspective, &removed_indices[perspective], &added_indices[perspective]);
        }
      }

      for (const auto perspective : {WHITE, BLACK}) {
        if (reset[perspective]) {
          (*index_sets)[i][perspective].clear();
          ++num_resets[i];
        } else {
          for (const auto index : removed_indices[perspective]) {
            ASSERT(index < RawFeatures::kDimensions);
            ASSERT((*index_sets)[i][perspective].count(index) == 1);
            ASSERT(trigger_map[index] == kUnknown || trigger_map[index] == i);
            (*index_sets)[i][perspective].erase(index);
            ++num_updates.back();
            ++num_updates[i];
            trigger_map[index] = i;
          }
        }
        for (const auto index : added_indices[perspective]) {
          ASSERT(index < RawFeatures::kDimensions);
          ASSERT((*index_sets)[i][perspective].count(index) == 0);
          ASSERT(trigger_map[index] == kUnknown || trigger_map[index] == i);
          (*index_sets)[i][perspective].insert(index);
          ++num_updates.back();
          ++num_updates[i];
          trigger_map[index] = i;
        }
      }
    }
  };

  std::cout << "feature set: " // << RawFeatures::GetName()
            << "[" << RawFeatures::kDimensions << "]" << std::endl;
  std::cout << "start testing with random games";

  for (std::uint64_t i = 0; i < num_games; ++i) {
    auto index_sets = make_index_sets(pos);
    for (ply = 0; ply < MAX_PLY; ++ply) {
      MoveList<LEGAL> mg(pos);

      if (mg.size() == 0)
        break;

      Move m = mg.begin()[prng.rand<unsigned>() % mg.size()];
      pos.do_move(m, state[ply]);

      ++num_moves;
      update_index_sets(pos, &index_sets);
      ASSERT(index_sets == make_index_sets(pos));
    }

    pos.set(variants.find(Options["UCI_Variant"])->second, startfen, false, &si, Threads.main(), sfen);

    if ((i % 100) == 0)
      std::cout << "." << std::flush;
  }

  std::cout << "passed." << std::endl;
  std::cout << num_games << " games, " << num_moves << " moves, "
            << num_updates.back() << " updates, "
            << (1.0 * num_updates.back() / num_moves)
            << " updates per move" << std::endl;
  std::size_t num_observed_indices = 0;
  for (IndexType i = 0; i < kRefreshTriggers.size(); ++i) {
    const auto count = std::count(trigger_map.begin(), trigger_map.end(), i);
    num_observed_indices += count;
    std::cout << "TriggerEvent(" << static_cast<int>(kRefreshTriggers[i])
              << "): " << count << " features ("
              << (100.0 * count / RawFeatures::kDimensions) << "%), "
              << num_updates[i] << " updates ("
              << (1.0 * num_updates[i] / num_moves) << " per move), "
              << num_resets[i] << " resets ("
              << (100.0 * num_resets[i] / num_moves) << "%)"
              << std::endl;
  }
  std::cout << "observed " << num_observed_indices << " ("
            << (100.0 * num_observed_indices / RawFeatures::kDimensions)
            << "% of " << RawFeatures::kDimensions
            << ") features" << std::endl;
}

// TODO
void PrintInfo(std::istream& stream) {
  std::cout << "Not Implemented" << std::endl;
}

}  // namespace

// UCI extended command for test NNUE
void TestCommand(Position& pos, std::istream& stream) {
  std::string sub_command;
  stream >> sub_command;

  if (sub_command == "test_features") {
    TestFeatures(pos);
  } else if (sub_command == "info") {
    PrintInfo(stream);
  } else {
    std::cout << "usage:" << std::endl;
    std::cout << " test nnue test_features" << std::endl;
    std::cout << " test nnue info [path/to/EvalFile...]" << std::endl;
  }
}

}  // namespace NNUE

}  // namespace Eval
