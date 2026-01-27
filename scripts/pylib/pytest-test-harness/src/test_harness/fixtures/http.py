# Copyright (c) 2024 Zephyr IO Project
# SPDX-License-Identifier: Apache-2.0

"""HTTP client fixtures for REST API testing."""

from __future__ import annotations

import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Generator

import pytest
import requests
from twister_harness import DeviceAdapter

logger = logging.getLogger(__name__)

DEFAULT_HOST = '127.0.0.1'
DEFAULT_PORT = 8080
DEFAULT_SCHEME = 'http'


@dataclass
class HttpConfig:
    """HTTP connection configuration parsed from fixture string."""
    host: str = DEFAULT_HOST
    port: int = DEFAULT_PORT
    scheme: str = DEFAULT_SCHEME

    @property
    def base_url(self) -> str:
        return f'{self.scheme}://{self.host}:{self.port}'

    @classmethod
    def from_fixture(cls, fixture_str: str) -> HttpConfig:
        """
        Parse HTTP configuration from fixture string.

        Format: http:ip:port or https:ip:port
        Example: http:192.168.1.100:8080
        """
        parts = fixture_str.split(':')
        if len(parts) < 3:
            raise ValueError(f'Invalid HTTP fixture format: {fixture_str}')
        return cls(host=parts[1], port=int(parts[2]), scheme=parts[0])


def parse_http_fixture(fixtures: list[str] | None) -> HttpConfig | None:
    """Parse http:ip:port or https:ip:port from fixtures list."""
    if not fixtures:
        return None
    for fixture in fixtures:
        if fixture.startswith('http:') or fixture.startswith('https:'):
            try:
                return HttpConfig.from_fixture(fixture)
            except (ValueError, IndexError) as e:
                logger.warning('Invalid HTTP fixture: %s (%s)', fixture, e)
    return None


class HttpClient:
    """HTTP client wrapper for device REST API testing."""

    def __init__(self, base_url: str, timeout: float = 10.0):
        self._base_url = base_url.rstrip('/')
        self._timeout = timeout
        self._session = requests.Session()

    @property
    def base_url(self) -> str:
        return self._base_url

    @property
    def timeout(self) -> float:
        return self._timeout

    def _url(self, path: str) -> str:
        """Build full URL from path."""
        if path.startswith('/'):
            return f'{self._base_url}{path}'
        return f'{self._base_url}/{path}'

    def get(
        self,
        path: str,
        params: dict[str, Any] | None = None,
        headers: dict[str, str] | None = None,
        timeout: float | None = None,
    ) -> requests.Response:
        """Send GET request."""
        url = self._url(path)
        logger.debug('GET %s', url)
        return self._session.get(
            url,
            params=params,
            headers=headers,
            timeout=timeout or self._timeout,
        )

    def post(
        self,
        path: str,
        data: dict[str, Any] | str | bytes | None = None,
        json: dict[str, Any] | list | None = None,
        files: dict[str, Any] | None = None,
        headers: dict[str, str] | None = None,
        timeout: float | None = None,
    ) -> requests.Response:
        """Send POST request."""
        url = self._url(path)
        logger.debug('POST %s', url)
        return self._session.post(
            url,
            data=data,
            json=json,
            files=files,
            headers=headers,
            timeout=timeout or self._timeout,
        )

    def put(
        self,
        path: str,
        data: dict[str, Any] | str | bytes | None = None,
        json: dict[str, Any] | list | None = None,
        files: dict[str, Any] | None = None,
        headers: dict[str, str] | None = None,
        timeout: float | None = None,
    ) -> requests.Response:
        """Send PUT request."""
        url = self._url(path)
        logger.debug('PUT %s', url)
        return self._session.put(
            url,
            data=data,
            json=json,
            files=files,
            headers=headers,
            timeout=timeout or self._timeout,
        )

    def delete(
        self,
        path: str,
        headers: dict[str, str] | None = None,
        timeout: float | None = None,
    ) -> requests.Response:
        """Send DELETE request."""
        url = self._url(path)
        logger.debug('DELETE %s', url)
        return self._session.delete(
            url,
            headers=headers,
            timeout=timeout or self._timeout,
        )

    def patch(
        self,
        path: str,
        data: dict[str, Any] | str | bytes | None = None,
        json: dict[str, Any] | list | None = None,
        headers: dict[str, str] | None = None,
        timeout: float | None = None,
    ) -> requests.Response:
        """Send PATCH request."""
        url = self._url(path)
        logger.debug('PATCH %s', url)
        return self._session.patch(
            url,
            data=data,
            json=json,
            headers=headers,
            timeout=timeout or self._timeout,
        )

    def upload_file(
        self,
        path: str,
        file_path: str | Path,
        field_name: str = 'file',
        content_type: str | None = None,
        extra_data: dict[str, Any] | None = None,
        timeout: float | None = None,
    ) -> requests.Response:
        """Upload a file via POST request."""
        file_path = Path(file_path)
        if not file_path.exists():
            raise FileNotFoundError(f'File not found: {file_path}')

        url = self._url(path)
        logger.info('Uploading %s to %s', file_path.name, url)

        with open(file_path, 'rb') as f:
            if content_type:
                files = {field_name: (file_path.name, f, content_type)}
            else:
                files = {field_name: (file_path.name, f)}

            return self._session.post(
                url,
                files=files,
                data=extra_data,
                timeout=timeout or self._timeout,
            )

    def download_file(
        self,
        path: str,
        save_path: str | Path,
        timeout: float | None = None,
    ) -> requests.Response:
        """Download a file via GET request."""
        url = self._url(path)
        logger.info('Downloading from %s to %s', url, save_path)

        response = self._session.get(
            url,
            stream=True,
            timeout=timeout or self._timeout,
        )

        if response.ok:
            save_path = Path(save_path)
            save_path.parent.mkdir(parents=True, exist_ok=True)
            with open(save_path, 'wb') as f:
                for chunk in response.iter_content(chunk_size=8192):
                    f.write(chunk)

        return response

    def close(self) -> None:
        """Close the HTTP session."""
        self._session.close()


@pytest.fixture
def http_config(dut: DeviceAdapter) -> HttpConfig:
    """Extract HTTP configuration from device fixtures or use defaults."""
    config = parse_http_fixture(dut.device_config.fixtures)
    if config is None:
        logger.info('HTTP config using defaults: %s://%s:%d', DEFAULT_SCHEME, DEFAULT_HOST, DEFAULT_PORT)
        return HttpConfig()

    logger.info('HTTP config from hardware map: %s', config.base_url)
    return config


@pytest.fixture
def http_client(
    http_config: HttpConfig,
    request: pytest.FixtureRequest
) -> Generator[HttpClient, None, None]:
    """Provide an HTTP client for device REST API testing."""
    marker = request.node.get_closest_marker('http_timeout')
    if marker and marker.args:
        timeout = float(marker.args[0])
    else:
        timeout = request.config.getoption('--http-timeout', default=10.0)

    client = HttpClient(http_config.base_url, timeout=timeout)
    logger.info('HTTP client ready: %s', http_config.base_url)

    try:
        yield client
    finally:
        client.close()
        logger.info('HTTP client closed')
