from skeleton.actions import FoldAction, CallAction, CheckAction, RaiseAction
from skeleton.states import GameState, TerminalState, RoundState
from skeleton.bot import Bot
from skeleton.runner import parse_args, run_bot

class Player(Bot):
    def handle_new_round(self, game_state, round_state, active):
        pass

    def handle_round_over(self, game_state, terminal_state, active):
        pass

    def get_action(self, game_state, round_state, active):
        legal_actions = round_state.legal_actions()
        my_cards = round_state.hands[active]
        
        # 1. Map card characters to numerical ranks so we can do math on them
        # 'T' is Ten, 'J' is Jack, etc.
        rank_map = {'2': 2, '3': 3, '4': 4, '5': 5, '6': 6, '7': 7, '8': 8, '9': 9, 'T': 10, 'J': 11, 'Q': 12, 'K': 13, 'A': 14}
        
        # Extract the ranks of our two hole cards
        r1 = rank_map[my_cards[0][0]]
        r2 = rank_map[my_cards[1][0]]
        
        high_card = max(r1, r2)
        low_card = min(r1, r2)
        is_pair = (r1 == r2)
        
        # 2. HEURISTIC: Define what makes a "Good" hand
        # - Any Pair (e.g., 55, 99, AA)
        # - Both cards are a Ten or higher (e.g., JT, KQ, AK)
        # - An Ace with a decent kicker (8 or higher)
        is_strong = is_pair or (low_card >= 10) or (high_card == 14 and low_card >= 8)

        # 3. Execute Strategy
        if is_strong:
            # We have a strong hand! Play aggressively, but don't go all-in instantly
            # Raising the minimum extracts chips safely without scaring them into folding
            if RaiseAction in legal_actions:
                min_raise, max_raise = round_state.raise_bounds()
                return RaiseAction(min_raise)
            
            # If we can't raise (e.g. cap reached), just call their bet
            if CallAction in legal_actions:
                return CallAction()
                
            if CheckAction in legal_actions:
                return CheckAction()
        else:
            # We have trash. Don't waste chips.
            if CheckAction in legal_actions:
                return CheckAction()  # Take a free card if the opponent lets us
            return FoldAction()       # Otherwise, run away!

if __name__ == '__main__':
    run_bot(Player(), parse_args())