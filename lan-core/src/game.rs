//! Turn-based mini-games over Noise (MVP: tic-tac-toe).

use serde::{Deserialize, Serialize};
use uuid::Uuid;

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[repr(u8)]
pub enum GameKind {
    TicTacToe = 1,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
pub enum Mark {
    X,
    O,
}

impl Mark {
    pub fn other(self) -> Self {
        match self {
            Mark::X => Mark::O,
            Mark::O => Mark::X,
        }
    }
    pub fn as_char(self) -> char {
        match self {
            Mark::X => 'X',
            Mark::O => 'O',
        }
    }
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
pub enum GameStatus {
    Pending,
    Active,
    Won(Mark),
    Draw,
    Declined,
    Resigned { by: Mark },
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TicTacToeGame {
    pub id: Uuid,
    pub peer_fp: String,
    /// Local player's mark (inviter is always X).
    pub my_mark: Mark,
    pub board: [Option<Mark>; 9],
    pub turn: Mark,
    pub status: GameStatus,
}

impl TicTacToeGame {
    pub fn new_invite(id: Uuid, peer_fp: String, as_inviter: bool) -> Self {
        Self {
            id,
            peer_fp,
            my_mark: if as_inviter { Mark::X } else { Mark::O },
            board: [None; 9],
            turn: Mark::X,
            status: if as_inviter {
                GameStatus::Pending
            } else {
                GameStatus::Pending
            },
        }
    }

    pub fn activate(&mut self) {
        self.status = GameStatus::Active;
        self.turn = Mark::X;
    }

    pub fn is_my_turn(&self) -> bool {
        matches!(self.status, GameStatus::Active) && self.turn == self.my_mark
    }

    pub fn play(&mut self, cell: u8, by: Mark) -> Result<(), String> {
        if !matches!(self.status, GameStatus::Active) {
            return Err("game not active".into());
        }
        if self.turn != by {
            return Err("not this player's turn".into());
        }
        let i = cell as usize;
        if i >= 9 {
            return Err("cell out of range".into());
        }
        if self.board[i].is_some() {
            return Err("cell occupied".into());
        }
        self.board[i] = Some(by);
        if let Some(w) = winner(&self.board) {
            self.status = GameStatus::Won(w);
        } else if self.board.iter().all(|c| c.is_some()) {
            self.status = GameStatus::Draw;
        } else {
            self.turn = by.other();
        }
        Ok(())
    }

    pub fn resign(&mut self, by: Mark) {
        self.status = GameStatus::Resigned { by };
    }

    pub fn board_string(&self) -> String {
        let cell = |i: usize| match self.board[i] {
            Some(m) => m.as_char(),
            None => char::from_digit(i as u32, 10).unwrap_or('.'),
        };
        format!(
            " {} | {} | {}\n---+---+---\n {} | {} | {}\n---+---+---\n {} | {} | {}",
            cell(0),
            cell(1),
            cell(2),
            cell(3),
            cell(4),
            cell(5),
            cell(6),
            cell(7),
            cell(8)
        )
    }
}

fn winner(board: &[Option<Mark>; 9]) -> Option<Mark> {
    const LINES: [[usize; 3]; 8] = [
        [0, 1, 2],
        [3, 4, 5],
        [6, 7, 8],
        [0, 3, 6],
        [1, 4, 7],
        [2, 5, 8],
        [0, 4, 8],
        [2, 4, 6],
    ];
    for line in LINES {
        if let (Some(a), Some(b), Some(c)) = (board[line[0]], board[line[1]], board[line[2]]) {
            if a == b && b == c {
                return Some(a);
            }
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn x_wins_row() {
        let mut g = TicTacToeGame::new_invite(Uuid::nil(), "p".into(), true);
        g.activate();
        g.play(0, Mark::X).unwrap();
        g.play(3, Mark::O).unwrap();
        g.play(1, Mark::X).unwrap();
        g.play(4, Mark::O).unwrap();
        g.play(2, Mark::X).unwrap();
        assert_eq!(g.status, GameStatus::Won(Mark::X));
    }

    #[test]
    fn reject_bad_move() {
        let mut g = TicTacToeGame::new_invite(Uuid::nil(), "p".into(), true);
        g.activate();
        g.play(0, Mark::X).unwrap();
        assert!(g.play(0, Mark::O).is_err());
        assert!(g.play(1, Mark::X).is_err()); // wrong turn
    }
}
