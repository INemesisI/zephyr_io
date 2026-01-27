# Copyright (c) 2024 Zephyr IO Project
# SPDX-License-Identifier: Apache-2.0

"""Flash fixtures for firmware update testing."""

from __future__ import annotations

import logging
import shutil
import subprocess
from pathlib import Path

import pytest
from twister_harness import DeviceAdapter
from twister_harness.twister_harness_config import DeviceConfig

logger = logging.getLogger(__name__)


def _get_runner_board_id_args(device_config: DeviceConfig) -> list[str]:
    """Get runner-specific board ID arguments (matches HardwareAdapter logic)."""
    args = []
    runner = device_config.runner
    board_id = device_config.id

    if not board_id:
        return args

    if runner == 'pyocd':
        args.extend(['--board-id', board_id])
    elif runner in ('nrfjprog', 'nrfutil'):
        args.extend(['--dev-id', board_id])
    elif runner == 'openocd' and device_config.product in ['STM32 STLink', 'STLINK-V3']:
        args.extend(['--cmd-pre-init', f'hla_serial {board_id}'])
    elif runner == 'openocd' and device_config.product == 'EDBG CMSIS-DAP':
        args.extend(['--cmd-pre-init', f'cmsis_dap_serial {board_id}'])
    elif runner == 'openocd' and device_config.product == 'LPC-LINK2 CMSIS-DAP':
        args.extend(['--cmd-pre-init', f'adapter serial {board_id}'])
    elif runner == 'jlink':
        args.append(f'-SelectEmuBySN {board_id}')
    elif runner == 'stm32cubeprogrammer':
        args.append(f'sn={board_id}')
    elif runner == 'linkserver':
        args.extend(['--probe', board_id])

    return args


def _build_flash_command(
    device_config: DeviceConfig,
    binary_path: Path,
    build_dir: Path | None = None,
    offset: int | str | None = None,
) -> list[str]:
    """Build west flash command for a custom binary."""
    west = shutil.which('west')
    if not west:
        raise RuntimeError('west not found in PATH')

    effective_build_dir = build_dir or device_config.build_dir

    command = [
        west, 'flash',
        '--skip-rebuild',
        '--build-dir', str(effective_build_dir),
    ]

    extra_args = []

    if device_config.runner:
        command.extend(['--runner', device_config.runner])
        extra_args.extend(_get_runner_board_id_args(device_config))

    if device_config.runner_params:
        extra_args.extend(device_config.runner_params)

    suffix = binary_path.suffix.lower()
    if suffix == '.bin':
        extra_args.extend(['--bin-file', str(binary_path)])
        if offset is not None:
            addr = hex(offset) if isinstance(offset, int) else offset
            extra_args.extend(['--load-addr', addr])
    elif suffix == '.hex':
        extra_args.extend(['--hex-file', str(binary_path)])
    elif suffix == '.elf':
        extra_args.extend(['--elf-file', str(binary_path)])
    else:
        extra_args.extend(['--bin-file', str(binary_path)])
        if offset is not None:
            addr = hex(offset) if isinstance(offset, int) else offset
            extra_args.extend(['--load-addr', addr])

    if extra_args:
        command.append('--')
        command.extend(extra_args)

    return command


class FlashDevice:
    """Helper class for flashing custom binaries to a device."""

    def __init__(self, device_config: DeviceConfig, timeout: float = 60.0):
        self._device_config = device_config
        self._timeout = timeout

    def flash(
        self,
        binary_path: str | Path,
        build_dir: str | Path | None = None,
        offset: int | str | None = None,
        timeout: float | None = None,
    ) -> None:
        """
        Flash a custom binary to the device.

        Args:
            binary_path: Path to the binary file (.bin, .hex, or .elf)
            build_dir: Optional build directory (uses device's build_dir if not specified)
            offset: Load address for .bin files (int or hex string like '0x8000')
            timeout: Flash timeout in seconds (uses default if not specified)
        """
        binary = Path(binary_path)
        if not binary.exists():
            raise FileNotFoundError(f'Binary not found: {binary}')

        bd = Path(build_dir) if build_dir else None
        command = _build_flash_command(self._device_config, binary, bd, offset)

        flash_timeout = timeout or self._timeout
        logger.info('Flashing %s to device %s', binary.name, self._device_config.id or 'unknown')
        logger.debug('Flash command: %s', ' '.join(command))

        try:
            result = subprocess.run(
                command,
                capture_output=True,
                timeout=flash_timeout,
                check=False,
            )
            if result.returncode != 0:
                stderr = result.stderr.decode(errors='ignore')
                raise RuntimeError(f'Flash failed: {stderr}')
            logger.info('Flash completed successfully')
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError(f'Flash timed out after {flash_timeout}s') from exc

    def __call__(
        self,
        binary_path: str | Path,
        build_dir: str | Path | None = None,
        offset: int | str | None = None,
        timeout: float | None = None,
    ) -> None:
        """Allow calling instance directly: flash_device("firmware.bin")"""
        self.flash(binary_path, build_dir, offset, timeout)


@pytest.fixture
def flash_device(dut: DeviceAdapter) -> FlashDevice:
    """Fixture providing a callable to flash custom binaries."""
    return FlashDevice(dut.device_config)
