"""Phase 2 - Knowledge Distillation (paper section 4).

Trains the lightweight student network to approximate the oracle's
probability surface. CPU-friendly by design: the deployed model must run
inside a frame budget, so the architecture stays deliberately small.
"""

from __future__ import annotations

import numpy as np
import torch
from torch import nn
from tqdm import trange

from qos_core.config import QOSConfig


class StudentNet(nn.Module):
    """Feed-forward approximator of the quantum decision surface.

    Default 3-64-64-1 with ReLU hidden activations and sigmoid output,
    matching the whitepaper (section 4.2).
    """

    def __init__(self, n_inputs: int, hidden: list[int]):
        super().__init__()
        layers: list[nn.Module] = []
        prev = n_inputs
        for width in hidden:
            layers += [nn.Linear(prev, width), nn.ReLU()]
            prev = width
        layers += [nn.Linear(prev, 1), nn.Sigmoid()]
        self.net = nn.Sequential(*layers)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.net(x)


def train_student(
    cfg: QOSConfig, x: np.ndarray, y: np.ndarray, progress: bool = True
) -> tuple[StudentNet, dict]:
    torch.manual_seed(cfg.seed)
    device = "cpu"  # the target platform for deployment; keeps runs reproducible

    n_val = max(1, int(len(x) * cfg.student.val_split))
    perm = np.random.default_rng(cfg.seed).permutation(len(x))
    val_idx, train_idx = perm[:n_val], perm[n_val:]

    x_t = torch.from_numpy(x[train_idx]).to(device)
    y_t = torch.from_numpy(y[train_idx]).unsqueeze(1).to(device)
    x_v = torch.from_numpy(x[val_idx]).to(device)
    y_v = torch.from_numpy(y[val_idx]).unsqueeze(1).to(device)

    model = StudentNet(cfg.oracle.n_qubits, cfg.student.hidden).to(device)
    optimizer = torch.optim.Adam(model.parameters(), lr=cfg.student.lr)
    loss_fn = nn.MSELoss()
    batch = cfg.student.batch_size

    epochs = trange(cfg.student.epochs, desc="distill", unit="epoch", disable=not progress)
    history = []
    for _ in epochs:
        model.train()
        epoch_perm = torch.randperm(len(x_t))
        for start in range(0, len(x_t), batch):
            idx = epoch_perm[start : start + batch]
            optimizer.zero_grad()
            loss = loss_fn(model(x_t[idx]), y_t[idx])
            loss.backward()
            optimizer.step()

        model.eval()
        with torch.no_grad():
            val_pred = model(x_v)
            val_mse = loss_fn(val_pred, y_v).item()
            val_mae = (val_pred - y_v).abs().mean().item()
        history.append({"val_mse": val_mse, "val_mae": val_mae})
        epochs.set_postfix(val_mae=f"{val_mae:.4f}")

    metrics = {
        "val_mae": history[-1]["val_mae"],
        "val_mse": history[-1]["val_mse"],
        "epochs": cfg.student.epochs,
        "n_train": int(len(train_idx)),
        "n_val": int(len(val_idx)),
        "history": history,
    }
    return model, metrics


def save_student(cfg: QOSConfig, model: StudentNet) -> None:
    cfg.run_dir.mkdir(parents=True, exist_ok=True)
    torch.save(
        {
            "state_dict": model.state_dict(),
            "n_inputs": cfg.oracle.n_qubits,
            "hidden": cfg.student.hidden,
            "inputs": cfg.oracle.inputs,
        },
        cfg.student_path,
    )


def load_student(cfg: QOSConfig) -> StudentNet:
    payload = torch.load(cfg.student_path, map_location="cpu", weights_only=True)
    model = StudentNet(payload["n_inputs"], payload["hidden"])
    model.load_state_dict(payload["state_dict"])
    model.eval()
    return model
