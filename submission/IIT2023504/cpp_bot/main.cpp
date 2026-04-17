#include <skeleton/actions.h>
#include <skeleton/constants.h>
#include <skeleton/runner.h>
#include <skeleton/states.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

using namespace pokerbots::skeleton;

namespace {

enum class Suit : uint8_t { Clubs = 0, Diamonds = 1, Hearts = 2, Spades = 3 };

struct Card {
  uint8_t rank; // 2..14 where 14 = Ace
  Suit suit;

  bool operator==(const Card &other) const {
    return rank == other.rank && suit == other.suit;
  }
};

static inline bool valid_card(const Card &c) {
  return c.rank >= 2 && c.rank <= 14;
}

static inline int card_id(const Card &c) {
  return static_cast<int>(c.suit) * 13 + (static_cast<int>(c.rank) - 2);
}

static inline int parse_rank(char r) {
  if (r >= '2' && r <= '9') return r - '0';
  if (r == 'T' || r == 't') return 10;
  if (r == 'J' || r == 'j') return 11;
  if (r == 'Q' || r == 'q') return 12;
  if (r == 'K' || r == 'k') return 13;
  if (r == 'A' || r == 'a') return 14;
  return -1;
}

static inline int parse_suit(char s) {
  if (s == 'c' || s == 'C') return static_cast<int>(Suit::Clubs);
  if (s == 'd' || s == 'D') return static_cast<int>(Suit::Diamonds);
  if (s == 'h' || s == 'H') return static_cast<int>(Suit::Hearts);
  if (s == 's' || s == 'S') return static_cast<int>(Suit::Spades);
  return -1;
}

static inline bool parse_card_token(const std::string &token, Card &out) {
  if (token.size() < 2) return false;
  const int r = parse_rank(token[0]);
  const int s = parse_suit(token[1]);
  if (r < 2 || s < 0) return false;
  out.rank = static_cast<uint8_t>(r);
  out.suit = static_cast<Suit>(s);
  return true;
}

struct MyGameState {
  std::array<Card, 2> hero_hole{};
  int hero_hole_count = 0;

  std::array<Card, 5> board{};
  int board_count = 0;

  int num_opponents = 1;
  int pot = 0;
  int to_call = 0;
  int hero_stack = 0;
  int starting_stack = STARTING_STACK;

  std::string focus_opponent = "villain";

  bool valid() const {
    if (hero_hole_count != 2) return false;
    if (board_count < 0 || board_count > 5) return false;
    if (num_opponents < 1 || num_opponents > 9) return false;

    if (!valid_card(hero_hole[0]) || !valid_card(hero_hole[1])) return false;

    bool used[52] = {false};
    const int h0 = card_id(hero_hole[0]);
    const int h1 = card_id(hero_hole[1]);
    if (h0 < 0 || h0 >= 52 || h1 < 0 || h1 >= 52 || h0 == h1) return false;
    used[h0] = true;
    used[h1] = true;

    for (int i = 0; i < board_count; ++i) {
      if (!valid_card(board[i])) return false;
      const int id = card_id(board[i]);
      if (id < 0 || id >= 52 || used[id]) return false;
      used[id] = true;
    }

    return true;
  }
};

namespace Eval {

static inline bool has_rank(uint16_t mask, int rank) {
  return (mask >> (rank - 2)) & 1U;
}

static inline int highest_straight_high_card(uint16_t rank_mask) {
  for (int high = 14; high >= 5; --high) {
    bool ok = true;
    for (int r = high; r >= high - 4; --r) {
      if (!has_rank(rank_mask, r)) {
        ok = false;
        break;
      }
    }
    if (ok) return high;
  }
  if (has_rank(rank_mask, 14) && has_rank(rank_mask, 2) && has_rank(rank_mask, 3) && has_rank(rank_mask, 4) && has_rank(rank_mask, 5)) {
    return 5;
  }
  return 0;
}

static inline int pack5(int a, int b = 0, int c = 0, int d = 0, int e = 0) {
  return (a << 16) | (b << 12) | (c << 8) | (d << 4) | e;
}

static inline int evaluate_n(const Card *cards, int n) {
  assert(n >= 5 && n <= 7);

  int rank_count[15] = {0};
  int suit_count[4] = {0};
  uint16_t suit_mask[4] = {0, 0, 0, 0};
  uint16_t rank_mask = 0;

  for (int i = 0; i < n; ++i) {
    const int r = cards[i].rank;
    const int s = static_cast<int>(cards[i].suit);
    ++rank_count[r];
    ++suit_count[s];
    const uint16_t bit = static_cast<uint16_t>(1U << (r - 2));
    suit_mask[s] |= bit;
    rank_mask |= bit;
  }

  int flush_suit = -1;
  for (int s = 0; s < 4; ++s) {
    if (suit_count[s] >= 5) {
      flush_suit = s;
      break;
    }
  }

  if (flush_suit != -1) {
    const int sf_high = highest_straight_high_card(suit_mask[flush_suit]);
    if (sf_high > 0) {
      return (8 << 20) | pack5(sf_high);
    }
  }

  int quads[2], qn = 0;
  int trips[3], tn = 0;
  int pairs[4], pn = 0;
  int singles[7], sn = 0;

  for (int r = 14; r >= 2; --r) {
    if (rank_count[r] == 4) quads[qn++] = r;
    else if (rank_count[r] == 3) trips[tn++] = r;
    else if (rank_count[r] == 2) pairs[pn++] = r;
    else if (rank_count[r] == 1) singles[sn++] = r;
  }

  if (qn > 0) {
    int kicker = 0;
    for (int r = 14; r >= 2; --r) {
      if (r != quads[0] && rank_count[r] > 0) {
        kicker = r;
        break;
      }
    }
    return (7 << 20) | pack5(quads[0], kicker);
  }

  if (tn > 0 && (pn > 0 || tn > 1)) {
    const int trip_rank = trips[0];
    const int pair_rank = (tn > 1) ? trips[1] : pairs[0];
    return (6 << 20) | pack5(trip_rank, pair_rank);
  }

  if (flush_suit != -1) {
    int flush_cards[7], fn = 0;
    for (int r = 14; r >= 2; --r) {
      if (has_rank(suit_mask[flush_suit], r)) {
        flush_cards[fn++] = r;
        if (fn == 5) break;
      }
    }
    return (5 << 20) | pack5(flush_cards[0], flush_cards[1], flush_cards[2], flush_cards[3], flush_cards[4]);
  }

  {
    const int st_high = highest_straight_high_card(rank_mask);
    if (st_high > 0) {
      return (4 << 20) | pack5(st_high);
    }
  }

  if (tn > 0) {
    int k1 = 0, k2 = 0;
    for (int r = 14; r >= 2; --r) {
      if (r == trips[0]) continue;
      if (rank_count[r] > 0) {
        if (!k1) k1 = r;
        else {
          k2 = r;
          break;
        }
      }
    }
    return (3 << 20) | pack5(trips[0], k1, k2);
  }

  if (pn >= 2) {
    int kicker = 0;
    for (int r = 14; r >= 2; --r) {
      if (r != pairs[0] && r != pairs[1] && rank_count[r] > 0) {
        kicker = r;
        break;
      }
    }
    return (2 << 20) | pack5(pairs[0], pairs[1], kicker);
  }

  if (pn == 1) {
    int k[3] = {0, 0, 0};
    int idx = 0;
    for (int r = 14; r >= 2 && idx < 3; --r) {
      if (r != pairs[0] && rank_count[r] > 0) {
        k[idx++] = r;
      }
    }
    return (1 << 20) | pack5(pairs[0], k[0], k[1], k[2]);
  }

  int h[5] = {0, 0, 0, 0, 0};
  int idx = 0;
  for (int r = 14; r >= 2 && idx < 5; --r) {
    if (rank_count[r] > 0) h[idx++] = r;
  }
  return pack5(h[0], h[1], h[2], h[3], h[4]);
}

static inline int evaluate7(const std::array<Card, 7> &cards) {
  return evaluate_n(cards.data(), 7);
}

} // namespace Eval

class MonteCarloEngine {
public:
  explicit MonteCarloEngine(uint32_t seed = std::random_device{}()) : rng_(seed) {}

  double estimate_equity(const MyGameState &state, int simulations, bool enable_early_stop = true, int max_time_us = 700000) {
    timed_out_last_ = false;
    sims_done_last_ = 0;

    if (!state.valid() || simulations <= 0) return 0.0;
    const auto start_ts = std::chrono::steady_clock::now();

    bool used[52] = {false};
    std::array<Card, 52> pool{};
    int pool_size = 0;

    for (int i = 0; i < state.hero_hole_count; ++i) {
      const int id = card_id(state.hero_hole[i]);
      if (used[id]) return 0.0;
      used[id] = true;
    }
    for (int i = 0; i < state.board_count; ++i) {
      const int id = card_id(state.board[i]);
      if (used[id]) return 0.0;
      used[id] = true;
    }

    for (int s = 0; s < 4; ++s) {
      for (int r = 2; r <= 14; ++r) {
        Card c{static_cast<uint8_t>(r), static_cast<Suit>(s)};
        if (!used[card_id(c)]) pool[pool_size++] = c;
      }
    }

    const int opp_count = state.num_opponents;
    const int board_missing = 5 - state.board_count;
    const int need_cards = opp_count * 2 + board_missing;
    if (need_cards > pool_size) return 0.0;

    uint64_t wins = 0;
    uint64_t ties = 0;

    std::array<Card, 52> sim_pool{};
    std::array<Card, 5> full_board{};
    std::array<std::array<Card, 2>, 9> opp_holes{};

    const bool fixed_board = (board_missing == 0);
    if (fixed_board) {
      for (int i = 0; i < 5; ++i) full_board[i] = state.board[i];
    }

    int hero_fixed_score = 0;
    if (fixed_board) {
      std::array<Card, 7> hero7{};
      hero7[0] = state.hero_hole[0];
      hero7[1] = state.hero_hole[1];
      for (int i = 0; i < 5; ++i) hero7[2 + i] = full_board[i];
      hero_fixed_score = Eval::evaluate7(hero7);
    }

    int total_done = 0;
    const int min_samples = std::min(simulations, 80);
    const int check_interval = 24;

    for (int sim = 0; sim < simulations; ++sim) {
      if (max_time_us > 0) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_ts).count();
        if (elapsed >= max_time_us && sim >= 24) {
          timed_out_last_ = true;
          break;
        }
      }

      std::copy(pool.begin(), pool.begin() + pool_size, sim_pool.begin());
      int live_size = pool_size;

      auto draw_random = [&](Card &out) {
        std::uniform_int_distribution<int> dist(0, live_size - 1);
        const int idx = dist(rng_);
        out = sim_pool[idx];
        sim_pool[idx] = sim_pool[live_size - 1];
        --live_size;
      };

      for (int o = 0; o < opp_count; ++o) {
        draw_random(opp_holes[o][0]);
        draw_random(opp_holes[o][1]);
      }

      if (!fixed_board) {
        for (int i = 0; i < state.board_count; ++i) full_board[i] = state.board[i];
        for (int i = state.board_count; i < 5; ++i) draw_random(full_board[i]);
      }

      int hero_score = hero_fixed_score;
      if (!fixed_board) {
        std::array<Card, 7> hero7{};
        hero7[0] = state.hero_hole[0];
        hero7[1] = state.hero_hole[1];
        for (int i = 0; i < 5; ++i) hero7[2 + i] = full_board[i];
        hero_score = Eval::evaluate7(hero7);
      }

      bool hero_best = true;
      bool hero_tied = false;

      for (int o = 0; o < opp_count; ++o) {
        std::array<Card, 7> opp7{};
        opp7[0] = opp_holes[o][0];
        opp7[1] = opp_holes[o][1];
        for (int i = 0; i < 5; ++i) opp7[2 + i] = full_board[i];

        const int opp_score = Eval::evaluate7(opp7);
        if (opp_score > hero_score) {
          hero_best = false;
          hero_tied = false;
          break;
        }
        if (opp_score == hero_score) {
          hero_tied = true;
        }
      }

      if (hero_best) {
        if (hero_tied) ++ties;
        else ++wins;
      }

      total_done = sim + 1;
      if (enable_early_stop && total_done >= min_samples && (total_done % check_interval == 0)) {
        const double eq = static_cast<double>(wins + 0.5 * ties) / static_cast<double>(total_done);
        if (eq >= 0.90 || eq <= 0.10) {
          break;
        }
      }
    }

    sims_done_last_ = total_done;
    if (total_done <= 0) return -1.0;
    return static_cast<double>(wins + 0.5 * ties) / static_cast<double>(total_done);
  }

  bool timed_out_last() const {
    return timed_out_last_;
  }

  int sims_done_last() const {
    return sims_done_last_;
  }

private:
  std::mt19937 rng_;
  bool timed_out_last_ = false;
  int sims_done_last_ = 0;
};

struct OpponentStats {
  int total_actions = 0;
  int folds = 0;
  int calls = 0;
  int raises = 0;

  int fold_to_raise = 0;
  int fold_to_raise_opportunities = 0;

  std::array<int, 4> street_actions{0, 0, 0, 0};
  std::array<int, 4> street_raises{0, 0, 0, 0};

  std::array<uint8_t, 5> recent_actions{0, 0, 0, 0, 0};
  int recent_count = 0;
  int recent_pos = 0;

  inline void push_recent(uint8_t a) {
    recent_actions[recent_pos] = a;
    recent_pos = (recent_pos + 1) % static_cast<int>(recent_actions.size());
    if (recent_count < static_cast<int>(recent_actions.size())) ++recent_count;
  }

  double fold_rate() const {
    return total_actions > 0 ? static_cast<double>(folds) / total_actions : 0.0;
  }

  double aggression() const {
    return total_actions > 0 ? static_cast<double>(raises) / total_actions : 0.0;
  }

  double call_rate() const {
    return total_actions > 0 ? static_cast<double>(calls) / total_actions : 0.0;
  }

  double fold_to_raise_rate() const {
    return fold_to_raise_opportunities > 0 ? static_cast<double>(fold_to_raise) / fold_to_raise_opportunities : 0.0;
  }

  double recent_aggression() const {
    if (recent_count <= 0) return 0.0;
    int raises_recent = 0;
    for (int i = 0; i < recent_count; ++i) {
      if (recent_actions[i] == 2) ++raises_recent;
    }
    return static_cast<double>(raises_recent) / recent_count;
  }

  bool is_tight() const {
    return fold_rate() > 0.45 && aggression() < 0.30;
  }

  bool is_loose() const {
    return fold_rate() < 0.28 && call_rate() > 0.48;
  }

  bool is_aggressive() const {
    return aggression() > 0.42 || recent_aggression() > 0.50;
  }
};

enum class ObservedActionType { Fold, Call, Raise };

class OpponentModel {
public:
  void observe_action(const std::string &name, ObservedActionType action, int street_idx = -1, bool faced_raise = false) {
    auto &s = stats_[name];
    ++s.total_actions;
    if (street_idx >= 0 && street_idx <= 3) {
      ++s.street_actions[street_idx];
    }

    if (faced_raise) {
      ++s.fold_to_raise_opportunities;
    }

    if (action == ObservedActionType::Fold) {
      ++s.folds;
      s.push_recent(0);
      if (faced_raise) ++s.fold_to_raise;
    } else if (action == ObservedActionType::Call) {
      ++s.calls;
      s.push_recent(1);
    } else {
      ++s.raises;
      s.push_recent(2);
      if (street_idx >= 0 && street_idx <= 3) ++s.street_raises[street_idx];
    }
  }

  OpponentStats get_stats(const std::string &name) const {
    auto it = stats_.find(name);
    if (it == stats_.end()) return OpponentStats{};
    return it->second;
  }

private:
  std::unordered_map<std::string, OpponentStats> stats_;
};

enum class BotActionType { Fold, Call, RaiseThirdPot, RaiseHalfPot, RaiseTwoThirdPot, RaisePot, AllIn };

static inline bool is_raise_action(BotActionType t) {
  return t == BotActionType::RaiseThirdPot ||
         t == BotActionType::RaiseHalfPot ||
         t == BotActionType::RaiseTwoThirdPot ||
         t == BotActionType::RaisePot;
}

static inline bool is_aggressive_action(BotActionType t) {
  return is_raise_action(t) || t == BotActionType::AllIn;
}

struct BoardTexture {
  bool flush_heavy = false;
  bool straight_heavy = false;
  bool paired = false;
  bool dangerous = false;
};

static inline int street_index_from_board_count(int board_count) {
  if (board_count <= 0) return 0;
  if (board_count == 3) return 1;
  if (board_count == 4) return 2;
  return 3;
}

static inline BoardTexture analyze_board_texture(const MyGameState &st) {
  BoardTexture t{};
  if (st.board_count < 3) return t;

  int suit_count[4] = {0};
  int rank_count[15] = {0};
  int uniq_ranks[5] = {0, 0, 0, 0, 0};
  int uniq_n = 0;

  for (int i = 0; i < st.board_count; ++i) {
    const Card c = st.board[i];
    ++suit_count[static_cast<int>(c.suit)];
    ++rank_count[c.rank];
  }

  for (int s = 0; s < 4; ++s) {
    if (suit_count[s] >= 3) {
      t.flush_heavy = true;
      break;
    }
  }

  for (int r = 2; r <= 14; ++r) {
    if (rank_count[r] > 0) uniq_ranks[uniq_n++] = r;
    if (rank_count[r] >= 2) t.paired = true;
  }

  if (uniq_n >= 3) {
    std::sort(uniq_ranks, uniq_ranks + uniq_n);
    for (int i = 0; i + 2 < uniq_n; ++i) {
      if (uniq_ranks[i + 2] - uniq_ranks[i] <= 4) {
        t.straight_heavy = true;
        break;
      }
    }
  }

  t.dangerous = t.flush_heavy || t.straight_heavy;
  return t;
}

struct QuickEquityHint {
  bool use_shortcut = false;
  double equity = 0.5;
};

static QuickEquityHint quick_equity_shortcut(const MyGameState &st) {
  QuickEquityHint hint{};
  if (!st.valid()) return hint;

  const Card a = st.hero_hole[0];
  const Card b = st.hero_hole[1];
  const int hi = std::max<int>(a.rank, b.rank);
  const int lo = std::min<int>(a.rank, b.rank);
  const bool pair = (a.rank == b.rank);
  const bool suited = (a.suit == b.suit);
  const int gap = hi - lo;

  if (st.board_count == 0) {
    double eq = 0.50;
    if (pair) {
      eq = 0.56 + (hi - 2) * 0.025;
    } else {
      eq = 0.34;
      if (hi >= 13) eq += 0.06;
      if (hi == 14 && lo >= 10) eq += 0.06;
      if (suited) eq += 0.03;
      eq -= std::min(0.07, static_cast<double>(gap) * 0.015);
      if (gap >= 5 && !suited) eq -= 0.04;
    }

    eq -= 0.075 * static_cast<double>(st.num_opponents - 1);
    eq = std::max(0.08, std::min(0.92, eq));

    if ((pair && hi >= 12) || (hi == 14 && lo >= 13 && suited)) {
      hint.use_shortcut = true;
      hint.equity = std::min(0.92, eq + 0.06);
      return hint;
    }
    if (!pair && !suited && hi <= 10 && gap >= 5 && eq < 0.25) {
      hint.use_shortcut = true;
      hint.equity = std::max(0.08, eq - 0.03);
      return hint;
    }
    return hint;
  }

  int rank_count[15] = {0};
  int suit_count[4] = {0};
  uint16_t rank_mask = 0;

  rank_count[a.rank]++;
  rank_count[b.rank]++;
  suit_count[static_cast<int>(a.suit)]++;
  suit_count[static_cast<int>(b.suit)]++;
  rank_mask |= static_cast<uint16_t>(1U << (a.rank - 2));
  rank_mask |= static_cast<uint16_t>(1U << (b.rank - 2));

  for (int i = 0; i < st.board_count; ++i) {
    const Card c = st.board[i];
    rank_count[c.rank]++;
    suit_count[static_cast<int>(c.suit)]++;
    rank_mask |= static_cast<uint16_t>(1U << (c.rank - 2));
  }

  int pairs = 0, trips = 0, quads = 0;
  for (int r = 2; r <= 14; ++r) {
    if (rank_count[r] == 4) ++quads;
    else if (rank_count[r] == 3) ++trips;
    else if (rank_count[r] == 2) ++pairs;
  }

  bool flush_draw = false;
  bool flush_made = false;
  for (int s = 0; s < 4; ++s) {
    if (suit_count[s] >= 5) flush_made = true;
    if (suit_count[s] == 4 && st.board_count < 5) flush_draw = true;
  }

  bool straight_made = (Eval::highest_straight_high_card(rank_mask) > 0);
  bool straight_draw = false;
  if (!straight_made && st.board_count < 5) {
    for (int high = 14; high >= 5; --high) {
      int cnt = 0;
      for (int r = high; r >= high - 4; --r) {
        if ((rank_mask >> (r - 2)) & 1U) ++cnt;
      }
      if (cnt >= 4) {
        straight_draw = true;
        break;
      }
    }
  }

  if (st.board_count == 5) {
    std::array<Card, 7> hero7{};
    hero7[0] = a;
    hero7[1] = b;
    for (int i = 0; i < 5; ++i) hero7[2 + i] = st.board[i];
    const int score = Eval::evaluate7(hero7);
    const int cat = score >> 20;

    if (cat >= 6) {
      hint.use_shortcut = true;
      hint.equity = 0.93;
    } else if (cat >= 4) {
      hint.use_shortcut = true;
      hint.equity = 0.82;
    } else if (cat == 3) {
      hint.use_shortcut = true;
      hint.equity = 0.74;
    } else if (cat <= 1) {
      hint.use_shortcut = true;
      hint.equity = 0.24;
    }
    return hint;
  }

  if (quads > 0 || trips > 0 || pairs >= 2 || flush_made || straight_made) {
    hint.use_shortcut = true;
    hint.equity = 0.80;
    return hint;
  }

  const bool weak_no_draw = (pairs == 0 && !flush_draw && !straight_draw && hi <= 11);
  if (weak_no_draw && st.board_count >= 3) {
    hint.use_shortcut = true;
    hint.equity = 0.20;
    return hint;
  }

  return hint;
}

struct BotAction {
  BotActionType type = BotActionType::Fold;
  int amount = 0;
};

class DecisionEngine {
public:
  explicit DecisionEngine(uint32_t seed = std::random_device{}()) : rng_(seed) {}

  void record_bluff_result(bool success) {
    ++bluff_attempts_;
    if (success) ++bluff_successes_;
  }

  double bluff_success_rate() const {
    return bluff_attempts_ > 0 ? static_cast<double>(bluff_successes_) / bluff_attempts_ : 0.5;
  }

  BotAction decide(const MyGameState &st, double equity, const OpponentStats &opp) {
    equity = std::max(0.0, std::min(1.0, equity + (rand01() - 0.5) * 0.02));

    const double pot_odds = compute_pot_odds(st);
    const BoardTexture texture = analyze_board_texture(st);
    BotAction action = make_fold_or_check(st);

    const bool facing_big_bet = (st.to_call > 0 && st.hero_stack > 0 && st.to_call * 10 >= 4 * st.hero_stack);
    if (facing_big_bet && equity < 0.75) {
      return make_fold_or_check(st);
    }

    const bool facing_near_allin = (st.to_call > 0 && st.hero_stack > 0 && st.to_call * 10 >= 8 * st.hero_stack);
    if (facing_near_allin && equity < 0.80) {
      return make_fold_or_check(st);
    }

    const bool very_low_stack = (st.starting_stack > 0 && st.hero_stack * 100 <= 12 * st.starting_stack);
    if (very_low_stack && st.to_call > 0 && equity < 0.50) {
      return make_fold_or_check(st);
    }

    const bool big_pot = (st.hero_stack > 0 && st.pot > st.hero_stack / 2);
    const bool very_big_pot = (st.hero_stack > 0 && st.pot >= (st.hero_stack * 3) / 4);
    const bool short_stack = (st.starting_stack > 0 && st.hero_stack * 10 <= 2 * st.starting_stack);

    if (st.board_count == 0) {
      action = decide_preflop(st, equity, opp, short_stack);
      if (big_pot && st.to_call > 0 && equity < std::max(0.72, pot_odds + 0.12)) {
        action = make_fold_or_check(st);
      }
      if (action.type == BotActionType::AllIn && equity < 0.62) {
        action = (st.to_call > 0 && equity < pot_odds + 0.02) ? make_fold_or_check(st) : make_call_or_check(st);
      }
      return finalize_action(st, action);
    }

    if (short_stack) {
      if (equity >= 0.62) {
        action = BotAction{BotActionType::AllIn, st.hero_stack};
      } else if (st.to_call > 0 && equity + 0.02 < pot_odds) {
        action = make_fold_or_check(st);
      }
    }

    const double risk_premium = big_pot ? 0.06 : 0.0;
    if (st.to_call > 0 && equity + 0.015 + risk_premium < pot_odds) {
      action = make_fold_or_check(st);
    } else if (equity >= 0.85) {
      action = choose_aggressive_value_bet(st, equity);
    } else if (equity >= 0.75) {
      action = weighted_mix(st, 0.01, 0.18, 0.04, 0.38, 0.30, 0.09);
    } else if (equity >= 0.70) {
      action = weighted_mix(st, 0.02, 0.22, 0.06, 0.39, 0.24, 0.07);
    } else if (equity >= std::max(0.60, pot_odds + 0.10)) {
      action = weighted_mix(st, 0.08, 0.48, 0.14, 0.20, 0.10, 0.00);
    } else if (equity >= pot_odds + 0.02) {
      action = weighted_mix(st, 0.20, 0.68, 0.05, 0.07, 0.00, 0.00);
    } else {
      action = make_fold_or_check(st);
    }

    const double fold_rate = opp.fold_rate();
    const double call_rate = opp.call_rate();
    const double aggression = opp.aggression();
    const double fold_to_raise = opp.fold_to_raise_rate();
    const double bluff_sr = bluff_success_rate();

    if (st.to_call == 0 && equity > 0.50 && equity < 0.70 && fold_to_raise > 0.56 && !texture.dangerous) {
      if (rand01() < 0.09) {
        action = make_raise_third_pot(st);
      }
    }

    const bool pot_small_for_bluff = (st.pot <= static_cast<int>(0.36 * st.hero_stack));
    const bool allow_bluff = (equity < 0.35 && fold_rate > 0.58 && pot_small_for_bluff && !texture.dangerous);

    if (allow_bluff || (equity < 0.40 && fold_to_raise > 0.55 && pot_small_for_bluff && !texture.dangerous)) {
      double bluff_p = (fold_to_raise > 0.60) ? 0.30 : 0.22;
      if (opp.is_tight()) bluff_p += 0.10;
      if (call_rate > 0.55 || opp.is_loose()) bluff_p -= 0.16;
      if (call_rate > 0.60) bluff_p = 0.0;
      if (texture.paired) bluff_p -= 0.03;
      if (bluff_attempts_ >= 5) {
        if (bluff_sr < 0.45) bluff_p *= 0.45;
        else if (bluff_sr > 0.60) bluff_p *= 1.20;
      }
      bluff_p = std::max(0.04, std::min(0.38, bluff_p));
      if (rand01() < bluff_p) {
        action = (rand01() < 0.60) ? make_raise_third_pot(st) : make_raise_two_thirds_pot(st);
      }
    }

    if (call_rate > 0.55 || opp.is_loose()) {
      if (equity < 0.55) {
        action = make_call_or_check(st);
        if (equity < 0.35) action = make_fold_or_check(st);
      } else if (equity > 0.66) {
        action = choose_aggressive_value_bet(st, equity);
      }
    }

    if ((aggression > 0.45 || opp.is_aggressive()) && equity > 0.72) {
      if (rand01() < 0.60) {
        action = make_call_or_check(st);
      }
    }

    if (opp.is_tight() && equity < 0.38 && pot_small_for_bluff && !texture.dangerous && rand01() < 0.20) {
      action = make_raise_third_pot(st);
    }

    if (texture.dangerous && equity < 0.72 && is_raise_action(action.type) && rand01() < 0.65) {
      action = make_call_or_check(st);
    }

    double dev = 0.10;
    if (equity < 0.35) dev = 0.10;
    else if (equity < 0.70) dev = 0.14;
    else dev = 0.12;

    if (texture.dangerous && equity < 0.70) dev *= 0.65;

    if (rand01() < dev) {
      action = random_legal_mix(st, equity, pot_odds);
    }

    if (action.type == BotActionType::Call && st.to_call > 0 && equity + 0.01 < pot_odds) {
      action = make_fold_or_check(st);
    }

    if (st.to_call > 0 && st.hero_stack > 0 && st.to_call * 10 >= 6 * st.hero_stack && equity < 0.78) {
      action = make_fold_or_check(st);
    }

    if (big_pot && st.to_call > 0 && equity < std::max(0.73, pot_odds + 0.10)) {
      action = make_fold_or_check(st);
    }
    if (very_big_pot && st.to_call > 0 && equity < std::max(0.79, pot_odds + 0.14)) {
      action = make_fold_or_check(st);
    }
    if (equity < 0.40 && big_pot && (is_raise_action(action.type) || action.type == BotActionType::AllIn)) {
      action = make_fold_or_check(st);
    }
    const bool very_high_stack = (st.starting_stack > 0 && st.hero_stack * 10 >= 17 * st.starting_stack);
    if (very_high_stack && action.type == BotActionType::AllIn && equity < 0.90) {
      action = choose_aggressive_value_bet(st, equity);
      if (action.type == BotActionType::AllIn) action = make_raise_pot(st);
    }
    if (action.type == BotActionType::AllIn && equity < 0.70) {
      action = (st.to_call > 0 && equity < pot_odds + 0.02) ? make_fold_or_check(st) : make_call_or_check(st);
    }

    return finalize_action(st, action);
  }

private:
  std::mt19937 rng_;
  std::uniform_real_distribution<double> unit_{0.0, 1.0};
  int bluff_attempts_ = 0;
  int bluff_successes_ = 0;

  inline double rand01() {
    return unit_(rng_);
  }

  int clamp_bet(const MyGameState &st, int target) {
    if (target <= st.to_call) return st.to_call;
    return std::min(target, st.hero_stack);
  }

  double compute_pot_odds(const MyGameState &st) const {
    if (st.to_call <= 0) return 0.0;
    const double denom = static_cast<double>(st.pot + st.to_call);
    if (denom <= 0.0) return 1.0;
    return static_cast<double>(st.to_call) / denom;
  }

  BotAction make_fold_or_check(const MyGameState &st) {
    if (st.to_call <= 0) return BotAction{BotActionType::Call, 0};
    return BotAction{BotActionType::Fold, 0};
  }

  BotAction make_call_or_check(const MyGameState &st) {
    const int amt = std::min(st.to_call, st.hero_stack);
    return BotAction{BotActionType::Call, amt};
  }

  BotAction make_raise_fraction(const MyGameState &st, int num, int den, BotActionType type) {
    const int base_extra = std::max(1, (st.pot * num) / den);
    int raise_to = st.to_call + base_extra;
    const double jitter = 0.90 + 0.25 * rand01();
    raise_to = static_cast<int>(raise_to * jitter);
    raise_to = clamp_bet(st, raise_to);
    if (raise_to >= st.hero_stack) return BotAction{BotActionType::AllIn, st.hero_stack};
    if (raise_to <= st.to_call) return make_call_or_check(st);
    return BotAction{type, raise_to};
  }

  BotAction make_raise_third_pot(const MyGameState &st) {
    return make_raise_fraction(st, 1, 3, BotActionType::RaiseThirdPot);
  }

  BotAction make_raise_half_pot(const MyGameState &st) {
    return make_raise_fraction(st, 1, 2, BotActionType::RaiseHalfPot);
  }

  BotAction make_raise_two_thirds_pot(const MyGameState &st) {
    return make_raise_fraction(st, 2, 3, BotActionType::RaiseTwoThirdPot);
  }

  BotAction make_raise_pot(const MyGameState &st) {
    return make_raise_fraction(st, 1, 1, BotActionType::RaisePot);
  }

  BotAction choose_aggressive_value_bet(const MyGameState &st, double equity) {
    if (equity > 0.85 && rand01() < 0.30) {
      return BotAction{BotActionType::AllIn, st.hero_stack};
    }
    if (equity >= 0.70) {
      if (rand01() < 0.55) return make_raise_pot(st);
      return make_raise_two_thirds_pot(st);
    }
    if (rand01() < 0.55) return make_raise_two_thirds_pot(st);
    return make_raise_half_pot(st);
  }

  BotAction weighted_mix(const MyGameState &st, double w_fold, double w_call, double w_r33, double w_r66, double w_r100, double w_allin) {
    const double total = w_fold + w_call + w_r33 + w_r66 + w_r100 + w_allin;
    double x = rand01() * (total > 0.0 ? total : 1.0);

    if ((x -= w_fold) <= 0.0) return make_fold_or_check(st);
    if ((x -= w_call) <= 0.0) return make_call_or_check(st);
    if ((x -= w_r33) <= 0.0) return make_raise_third_pot(st);
    if ((x -= w_r66) <= 0.0) return make_raise_two_thirds_pot(st);
    if ((x -= w_r100) <= 0.0) return make_raise_pot(st);
    if (w_allin > 0.0) return BotAction{BotActionType::AllIn, st.hero_stack};
    return make_call_or_check(st);
  }

  BotAction random_legal_mix(const MyGameState &st, double equity, double pot_odds) {
    if (equity < 0.35) {
      if (pot_odds > 0.30) return weighted_mix(st, 0.78, 0.20, 0.02, 0.00, 0.00, 0.00);
      return weighted_mix(st, 0.68, 0.22, 0.10, 0.00, 0.00, 0.00);
    }
    if (equity < 0.70) {
      return weighted_mix(st, 0.16, 0.58, 0.08, 0.14, 0.04, 0.00);
    }
    return weighted_mix(st, 0.04, 0.28, 0.05, 0.26, 0.30, 0.07);
  }

  BotAction decide_preflop(const MyGameState &st, double equity, const OpponentStats &opp, bool short_stack) {
    const Card a = st.hero_hole[0];
    const Card b = st.hero_hole[1];
    const int hi = std::max<int>(a.rank, b.rank);
    const int lo = std::min<int>(a.rank, b.rank);
    const bool pair = (a.rank == b.rank);
    const bool suited = (a.suit == b.suit);

    const bool strong = (pair && hi >= 12) || (hi == 14 && lo == 13);
    const bool medium = (pair && hi >= 9) || (hi == 14 && lo >= 11) || (hi == 13 && lo == 12);
    const bool weak = !strong && !medium;

    if (short_stack) {
      if (strong || equity >= 0.58) return BotAction{BotActionType::AllIn, st.hero_stack};
      if (st.to_call > 0 && equity < 0.44) return make_fold_or_check(st);
      return make_call_or_check(st);
    }

    if (strong) {
      if (st.to_call == 0 && rand01() < 0.18) return make_raise_half_pot(st);
      return (rand01() < 0.60) ? make_raise_pot(st) : make_raise_two_thirds_pot(st);
    }

    if (medium) {
      if (opp.is_loose()) {
        return (rand01() < 0.58) ? make_raise_two_thirds_pot(st) : make_call_or_check(st);
      }
      if (opp.is_aggressive()) {
        return (rand01() < 0.58) ? make_call_or_check(st) : make_raise_half_pot(st);
      }
      return (rand01() < 0.52) ? make_raise_half_pot(st) : make_call_or_check(st);
    }

    if (weak) {
      if (st.to_call > 0 && (equity < 0.42 || st.to_call > st.hero_stack / 5)) {
        return make_fold_or_check(st);
      }
      if (suited && hi >= 11 && rand01() < 0.22 && st.to_call == 0) {
        return make_raise_third_pot(st);
      }
      return make_call_or_check(st);
    }

    return make_call_or_check(st);
  }

  BotAction finalize_action(const MyGameState &st, BotAction a) {
    if (a.type == BotActionType::AllIn) {
      a.amount = st.hero_stack;
      return a;
    }
    if (a.type == BotActionType::Call) {
      a.amount = std::min(st.to_call, st.hero_stack);
      return a;
    }
    if (a.type == BotActionType::RaiseThirdPot ||
        a.type == BotActionType::RaiseHalfPot ||
        a.type == BotActionType::RaiseTwoThirdPot ||
        a.type == BotActionType::RaisePot) {
      if (a.amount >= st.hero_stack) {
        return BotAction{BotActionType::AllIn, st.hero_stack};
      }
      if (a.amount <= st.to_call) {
        return make_call_or_check(st);
      }
    }
    return a;
  }
};

static inline MyGameState adapt_state(GameInfoPtr gameState, RoundStatePtr roundState, int active) {
  MyGameState st;
  st.num_opponents = 1;
  st.starting_stack = STARTING_STACK;

  Card c0{}, c1{};
  if (parse_card_token(roundState->hands[active][0], c0) && parse_card_token(roundState->hands[active][1], c1) && !(c0 == c1)) {
    st.hero_hole[0] = c0;
    st.hero_hole[1] = c1;
    st.hero_hole_count = 2;
  }

  const int board_count_limit = std::max(0, std::min(5, roundState->street));
  st.board_count = 0;
  for (int i = 0; i < board_count_limit; ++i) {
    Card bc{};
    if (parse_card_token(roundState->deck[i], bc)) {
      st.board[st.board_count++] = bc;
    }
  }

  const int myPip = roundState->pips[active];
  const int oppPip = roundState->pips[1 - active];
  const int myStack = roundState->stacks[active];
  const int oppStack = roundState->stacks[1 - active];
  const int continueCost = std::max(0, oppPip - myPip);
  const int myContribution = STARTING_STACK - myStack;
  const int oppContribution = STARTING_STACK - oppStack;

  st.pot = std::max(0, myContribution + oppContribution);
  st.to_call = continueCost;
  st.hero_stack = std::max(0, myStack);
  st.starting_stack = STARTING_STACK;
  st.focus_opponent = "villain";

  (void)gameState;
  return st;
}

static inline int compute_sim_budget(const MyGameState &st, std::mt19937 &rng) {
  int lo = 80, hi = 120;
  if (st.board_count == 0) {
    lo = 200;
    hi = 250;
  } else if (st.board_count == 3) {
    lo = 150;
    hi = 200;
  } else if (st.board_count == 4) {
    lo = 120;
    hi = 150;
  }
  return std::uniform_int_distribution<int>(lo, hi)(rng);
}

static inline Action map_to_engine_action(
    const BotAction &a,
    const MyGameState &st,
    const std::unordered_set<Action::Type> &legal,
    int myPip,
    int continueCost,
    const std::array<int, 2> &raiseBounds) {
  const bool canRaise = legal.find(Action::Type::RAISE) != legal.end();
  const bool canCall = legal.find(Action::Type::CALL) != legal.end();
  const bool canCheck = legal.find(Action::Type::CHECK) != legal.end();

  auto fallback_call_or_check = [&]() {
    if (continueCost == 0 && canCheck) return Action{Action::Type::CHECK};
    if (canCall) return Action{Action::Type::CALL};
    if (canCheck) return Action{Action::Type::CHECK};
    return Action{Action::Type::FOLD};
  };

  if (a.type == BotActionType::Fold) {
    if (continueCost == 0 && canCheck) return Action{Action::Type::CHECK};
    return Action{Action::Type::FOLD};
  }

  if (a.type == BotActionType::Call) {
    return fallback_call_or_check();
  }

  if (!canRaise) {
    return fallback_call_or_check();
  }

  if (raiseBounds[0] <= myPip + continueCost) {
    return fallback_call_or_check();
  }
  if (raiseBounds[0] < 0 || raiseBounds[1] < raiseBounds[0]) {
    return fallback_call_or_check();
  }

  int target = raiseBounds[0];
  if (a.type == BotActionType::RaiseThirdPot) {
    target = myPip + continueCost + std::max(1, st.pot / 3);
  } else if (a.type == BotActionType::RaiseHalfPot) {
    target = myPip + continueCost + std::max(1, st.pot / 2);
  } else if (a.type == BotActionType::RaiseTwoThirdPot) {
    target = myPip + continueCost + std::max(1, (2 * st.pot) / 3);
  } else if (a.type == BotActionType::RaisePot) {
    target = myPip + continueCost + std::max(1, st.pot);
  } else if (a.type == BotActionType::AllIn) {
    target = myPip + st.hero_stack;
  }

  target = std::max(raiseBounds[0], std::min(target, raiseBounds[1]));
  return Action{Action::Type::RAISE, target};
}

struct Bot {
  MonteCarloEngine mc_engine;
  OpponentModel opp_model;
  DecisionEngine decision_engine;
  std::mt19937 budget_rng{std::random_device{}()};
  bool debug_mode = false;

  Bot() {
    if (const char *env = std::getenv("POKER_DEBUG")) {
      debug_mode = (std::string(env) != "0");
    }
  }

  void handleNewRound(GameInfoPtr gameState, RoundStatePtr roundState, int active) {
    (void)gameState;
    (void)roundState;
    (void)active;
  }

  void handleRoundOver(GameInfoPtr gameState, TerminalStatePtr terminalState, int active) {
    auto prev = std::static_pointer_cast<const RoundState>(terminalState->previousState);
    const int myDelta = terminalState->deltas[active];

    // Lightweight opponent model updates from showdown visibility/fold patterns.
    // If opponent cards are hidden and we won, infer likely fold by opponent.
    const bool oppCardsHidden = prev->hands[1 - active][0].empty() || prev->hands[1 - active][1].empty();
    if (oppCardsHidden && myDelta > 0) {
      opp_model.observe_action("villain", ObservedActionType::Fold, street_index_from_board_count(prev->street), false);
      decision_engine.record_bluff_result(true);
    } else if (!oppCardsHidden) {
      opp_model.observe_action("villain", ObservedActionType::Call, street_index_from_board_count(prev->street), false);
      if (myDelta < 0) {
        decision_engine.record_bluff_result(false);
      }
    }
    (void)gameState;
  }

  Action getAction(GameInfoPtr gameState, RoundStatePtr roundState, int active) {
    auto legalActions = roundState->legalActions();
    const int myPip = roundState->pips[active];
    const int oppPip = roundState->pips[1 - active];
    const int continueCost = std::max(0, oppPip - myPip);

    // Observe immediate opponent aggression when they put us to a decision.
    if (continueCost > 0) {
      opp_model.observe_action("villain", ObservedActionType::Raise, street_index_from_board_count(roundState->street), false);
    } else if (roundState->button > 0) {
      opp_model.observe_action("villain", ObservedActionType::Call, street_index_from_board_count(roundState->street), false);
    }

    std::array<int, 2> raiseBounds{0, 0};
    if (legalActions.find(Action::Type::RAISE) != legalActions.end()) {
      raiseBounds = roundState->raiseBounds();
    }

    const MyGameState st = adapt_state(gameState, roundState, active);
    if (!st.valid()) {
      if (legalActions.find(Action::Type::CHECK) != legalActions.end()) return Action{Action::Type::CHECK};
      if (legalActions.find(Action::Type::CALL) != legalActions.end()) return Action{Action::Type::CALL};
      return Action{Action::Type::FOLD};
    }

    int sims = compute_sim_budget(st, budget_rng);
    if (gameState->gameClock < 10.0) sims = std::min(sims, 100);
    if (gameState->gameClock < 4.0) sims = std::min(sims, 60);
    if (gameState->gameClock < 1.5) sims = std::min(sims, 30);

    const QuickEquityHint hint = quick_equity_shortcut(st);
    double equity = hint.equity;
    bool used_mc_fallback = false;
    if (!hint.use_shortcut) {
      equity = mc_engine.estimate_equity(st, sims, true, 550000);
      if (equity < 0.0 || mc_engine.timed_out_last()) {
        used_mc_fallback = true;
        equity = std::max(0.10, std::min(0.90, hint.equity));
      }
    }

    const OpponentStats opp = opp_model.get_stats("villain");
    const BotAction myAction = decision_engine.decide(st, equity, opp);
    const Action out = map_to_engine_action(myAction, st, legalActions, myPip, continueCost, raiseBounds);

    if (debug_mode) {
      std::cerr << "[DEBUG] round=" << gameState->roundNum
                << " street=" << roundState->street
                << " eq=" << equity
                << " shortcut=" << (hint.use_shortcut ? 1 : 0)
                << " sims_req=" << (hint.use_shortcut ? 0 : sims)
                << " sims_done=" << (hint.use_shortcut ? 0 : mc_engine.sims_done_last())
                << " mc_timeout=" << (hint.use_shortcut ? 0 : (mc_engine.timed_out_last() ? 1 : 0))
                << " mc_fallback=" << (used_mc_fallback ? 1 : 0)
                << " pot=" << st.pot
                << " to_call=" << st.to_call
                << " opp_fold=" << opp.fold_rate()
                << " opp_f2r=" << opp.fold_to_raise_rate()
                << " opp_agg=" << opp.aggression()
                << " bluff_sr=" << decision_engine.bluff_success_rate()
                << " action=" << static_cast<int>(out.actionType)
                << " amt=" << out.amount
                << "\n";
    }

    return out;
  }
};

static inline void build_full_deck(std::array<Card, 52> &deck) {
  int idx = 0;
  for (int s = 0; s < 4; ++s) {
    for (int r = 2; r <= 14; ++r) {
      deck[idx++] = Card{static_cast<uint8_t>(r), static_cast<Suit>(s)};
    }
  }
}

static inline MyGameState random_state_for_test(std::mt19937 &rng) {
  std::array<Card, 52> deck{};
  build_full_deck(deck);
  std::shuffle(deck.begin(), deck.end(), rng);

  MyGameState st;
  st.hero_hole[0] = deck[0];
  st.hero_hole[1] = deck[1];
  st.hero_hole_count = 2;

  const int bc_opt[4] = {0, 3, 4, 5};
  const int bc = bc_opt[std::uniform_int_distribution<int>(0, 3)(rng)];
  st.board_count = bc;
  for (int i = 0; i < bc; ++i) st.board[i] = deck[2 + i];

  st.starting_stack = STARTING_STACK;
  st.hero_stack = std::uniform_int_distribution<int>(25, STARTING_STACK)(rng);
  st.pot = std::uniform_int_distribution<int>(4, STARTING_STACK * 2)(rng);
  st.to_call = std::uniform_int_distribution<int>(0, st.hero_stack)(rng);
  st.num_opponents = 1;
  st.focus_opponent = "villain";
  return st;
}

static inline OpponentStats make_profile(double fold_rate, double call_rate, double raise_rate) {
  OpponentStats s;
  const int N = 200;
  s.total_actions = N;
  s.folds = static_cast<int>(fold_rate * N);
  s.calls = static_cast<int>(call_rate * N);
  s.raises = std::max(0, N - s.folds - s.calls);
  if (s.folds + s.calls + s.raises < N) s.calls += N - (s.folds + s.calls + s.raises);
  s.fold_to_raise_opportunities = 100;
  s.fold_to_raise = static_cast<int>(fold_rate * 100.0);
  return s;
}

static inline double eval_action_ev(const BotAction &a, const MyGameState &st, double eq, double opp_fold_prob, std::mt19937 &rng) {
  const bool aggressive = is_aggressive_action(a.type);
  const int invest_call = std::max(0, std::min(st.to_call, st.hero_stack));
  int invest = invest_call;
  if (aggressive) {
    invest = std::max(invest_call, std::min(st.hero_stack, std::max(st.to_call, a.amount)));
  }

  if (a.type == BotActionType::Fold) {
    if (eq > 0.60) return -0.20 * st.pot;
    return 0.0;
  }

  if (aggressive && eq < 0.45) {
    if (std::uniform_real_distribution<double>(0.0, 1.0)(rng) < opp_fold_prob) {
      return 0.55 * st.pot;
    }
  }

  return eq * (st.pot + invest) - (1.0 - eq) * invest;
}

static inline int run_internal_test_mode(int games) {
  std::mt19937 rng(20260416);
  MonteCarloEngine mc(1337);
  DecisionEngine de(2026);

  const std::vector<std::pair<std::string, OpponentStats>> profiles = {
      {"tight", make_profile(0.62, 0.24, 0.14)},
      {"loose", make_profile(0.20, 0.58, 0.22)},
      {"aggressive", make_profile(0.22, 0.26, 0.52)}};

  std::cout << "[SELFTEST] games=" << games << "\n";
  for (const auto &entry : profiles) {
    const std::string &name = entry.first;
    const OpponentStats &opp = entry.second;
    int wins = 0, losses = 0;
    double total_ev = 0.0;

    for (int i = 0; i < games; ++i) {
      MyGameState st = random_state_for_test(rng);
      const QuickEquityHint hint = quick_equity_shortcut(st);
      double eq = hint.use_shortcut ? hint.equity : mc.estimate_equity(st, compute_sim_budget(st, rng), true, 250000);
      if (eq < 0.0) eq = std::max(0.10, std::min(0.90, hint.equity));

      const BotAction a = de.decide(st, eq, opp);
      const double ev = eval_action_ev(a, st, eq, opp.fold_rate(), rng);
      total_ev += ev;
      if (ev >= 0.0) ++wins;
      else ++losses;
    }

    std::cout << "[SELFTEST] vs=" << name
              << " wins=" << wins
              << " losses=" << losses
              << " avg_ev=" << (total_ev / std::max(1, games))
              << "\n";
  }
  return 0;
}

} // namespace

/*
  Main program for running a C++ pokerbot.
*/
int main(int argc, char *argv[]) {
  if (const char *env = std::getenv("POKER_SELFTEST")) {
    if (std::string(env) != "0") {
      return run_internal_test_mode(30);
    }
  }
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg == "--selftest") {
      int games = 30;
      if (i + 1 < argc) {
        try {
          games = std::stoi(argv[i + 1]);
        } catch (...) {
          games = 30;
        }
      }
      games = std::max(20, std::min(50, games));
      return run_internal_test_mode(games);
    }
  }

  auto [host, port] = parseArgs(argc, argv);
  runBot<Bot>(host, port);
  return 0;
}
