"""Shared PC-local cache support for Gaea 2 agent scripts."""

import os
import tempfile
import warnings


class GaeaCacheError(RuntimeError):
    """The shared Gaea cache could not be resolved or accessed."""


def _legacy_cache_dir():
    return os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                         '..', 'cache'))


def cache_dir():
    local_app_data = os.environ.get('LOCALAPPDATA')
    if not local_app_data:
        raise GaeaCacheError('LOCALAPPDATA is missing; cannot resolve shared Gaea cache.')
    path = os.path.join(local_app_data, 'BrokenEngine', 'AgentCache', 'Gaea2')
    try:
        os.makedirs(path, exist_ok=True)
        if not os.path.isdir(path):
            raise OSError('cache path is not a directory')
    except OSError as error:
        raise GaeaCacheError(f'Cannot create shared Gaea cache {path}: {error}') from error
    return path


def _temporary_path(destination, data):
    descriptor = None
    temporary = None
    try:
        descriptor, temporary = tempfile.mkstemp(prefix=f'.{os.path.basename(destination)}.',
                                                  suffix='.tmp',
                                                  dir=os.path.dirname(destination))
        output = os.fdopen(descriptor, 'wb')
        descriptor = None
        with output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        return temporary
    except OSError as error:
        if descriptor is not None:
            try:
                os.close(descriptor)
            except OSError:
                pass
        if temporary is not None:
            try:
                os.unlink(temporary)
            except OSError:
                pass
        raise GaeaCacheError(f'Cannot stage shared Gaea cache file {destination}: {error}') from error


def atomic_write_bytes(destination, data):
    """Crash-safely replace destination with data."""
    temporary = _temporary_path(destination, data)
    try:
        os.replace(temporary, destination)
    except OSError as error:
        raise GaeaCacheError(f'Cannot replace shared Gaea cache file {destination}: {error}') from error
    finally:
        try:
            os.unlink(temporary)
        except OSError:
            pass


def atomic_write_text(destination, text):
    atomic_write_bytes(destination, text.encode('utf-8'))


def cache_file(name):
    """Resolve a named cache file and migrate its checkout-local predecessor."""
    destination = os.path.join(cache_dir(), name)
    legacy = os.path.join(_legacy_cache_dir(), name)
    if not os.path.isfile(legacy):
        return destination

    try:
        with open(legacy, 'rb') as source:
            legacy_data = source.read()
    except OSError as error:
        raise GaeaCacheError(f'Cannot read legacy Gaea cache file {legacy}: {error}') from error

    if not os.path.exists(destination):
        temporary = _temporary_path(destination, legacy_data)
        try:
            os.rename(temporary, destination)
        except FileExistsError:
            pass
        except OSError as error:
            raise GaeaCacheError(f'Cannot establish shared Gaea cache file {destination}: {error}') from error
        finally:
            try:
                os.unlink(temporary)
            except OSError:
                pass

    try:
        with open(destination, 'rb') as winner:
            winner_data = winner.read()
    except OSError as error:
        raise GaeaCacheError(f'Cannot read shared Gaea cache file {destination}: {error}') from error

    if winner_data != legacy_data:
        warnings.warn(f'Legacy Gaea cache differs from shared winner; preserving {legacy}',
                      RuntimeWarning)
    return destination
