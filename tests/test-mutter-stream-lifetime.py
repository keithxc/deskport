#!/usr/bin/env python3
"""Compile Mutter's actual close dispatcher against a GObject lifetime fixture.

Usage: test-mutter-stream-lifetime.py /path/to/mutter/src/backends/meta-stream-source.c
The unpatched dispatcher must fail before the compositor patch is tested.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = Path(sys.argv[1]).resolve()

def function(text):
    start = text.index('static gboolean\npipewire_loop_source_dispatch (')
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

preamble = r'''
#include <glib-object.h>
#include <stdlib.h>
typedef struct _MetaStreamSource MetaStreamSource;
typedef struct _MetaStream { GObject parent; MetaStreamSource *source; } MetaStream;
typedef struct { GObjectClass parent; } MetaStreamClass;
typedef struct { MetaStream *stream; gboolean emit_closed_after_dispatch; } MetaStreamSourcePrivate;
struct _MetaStreamSource { GObject parent; MetaStreamSourcePrivate priv; };
typedef struct { GObjectClass parent; } MetaStreamSourceClass;
static gboolean stream_alive;
static int source_disposals;
static MetaStream *session_stream;
static guint signals[1];
enum { CLOSED };
G_DEFINE_TYPE (MetaStream, meta_stream, G_TYPE_OBJECT)
G_DEFINE_TYPE (MetaStreamSource, meta_stream_source, G_TYPE_OBJECT)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaStream, g_object_unref)
static void stream_finalize (GObject *object) {
    stream_alive = FALSE;
    G_OBJECT_CLASS (meta_stream_parent_class)->finalize (object);
}
static void meta_stream_class_init (MetaStreamClass *klass) {
    G_OBJECT_CLASS (klass)->finalize = stream_finalize;
}
static void meta_stream_init (MetaStream *stream) { stream_alive = TRUE; }
static void source_dispose (GObject *object) {
    /* Avoid undefined behavior in the negative control. The real virtual
     * source dereferences its borrowed stream here to obtain the backend. */
    if (!stream_alive) exit (17);
    source_disposals++;
    G_OBJECT_CLASS (meta_stream_source_parent_class)->dispose (object);
}
static void meta_stream_source_class_init (MetaStreamSourceClass *klass) {
    G_OBJECT_CLASS (klass)->dispose = source_dispose;
    signals[CLOSED] = g_signal_new ("closed", G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}
static void meta_stream_source_init (MetaStreamSource *source) {}
static MetaStreamSourcePrivate *meta_stream_source_get_instance_private (MetaStreamSource *source) {
    return &source->priv;
}
typedef struct { GSource base; MetaStreamSource *source; void *pipewire_loop; } MetaPipeWireSource;
static int pw_loop_iterate (void *loop, int timeout) { return 0; }
static const char *spa_strerror (int error) { return "fixture"; }
static void on_source_closed (MetaStreamSource *source, MetaStream *stream) {
    /* meta_stream_stop drops the source, then the session drops the stream. */
    g_clear_object (&stream->source);
    g_clear_object (&session_stream);
}
'''
# G_DEFINE_TYPE supplies get_instance_private; use a named fixture accessor.
preamble = preamble.replace('static MetaStreamSourcePrivate *meta_stream_source_get_instance_private',
                            'static MetaStreamSourcePrivate *fixture_private')
main = r'''
int main (void) {
    for (int i = 0; i < 1000; i++) {
        session_stream = g_object_new (meta_stream_get_type (), NULL);
        MetaStreamSource *source = g_object_new (meta_stream_source_get_type (), NULL);
        session_stream->source = source;
        source->priv.stream = session_stream;
        g_signal_connect (source, "closed", G_CALLBACK (on_source_closed), session_stream);
        MetaPipeWireSource dispatch = { .source = source };
        /* Ordinary dispatch must retain ownership without closing anything. */
        pipewire_loop_source_dispatch ((GSource *) &dispatch, NULL, NULL);
        g_assert_true (stream_alive);
        g_assert_nonnull (session_stream);
        source->priv.emit_closed_after_dispatch = TRUE;
        pipewire_loop_source_dispatch ((GSource *) &dispatch, NULL, NULL);
        g_assert_null (session_stream);
        g_assert_false (stream_alive);
        g_assert_cmpint (source_disposals, ==, i + 1);
    }
}
'''
flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'gobject-2.0'], text=True))
with tempfile.TemporaryDirectory(prefix='deskport-mutter-close-') as temp:
    directory = Path(temp)
    target = directory / 'src/backends/meta-stream-source.c'
    target.parent.mkdir(parents=True)
    target.write_bytes(source.read_bytes())
    for patched in (False, True):
        if patched:
            subprocess.run(['patch', '-p1', '-i', str(root / 'host/linux/patches/mutter-51-stream-close-lifetime.patch')],
                           cwd=directory, check=True)
        actual = function(target.read_text()).replace('meta_stream_source_get_instance_private (source)', 'fixture_private (source)')
        cfile, binary = directory / 'lifetime.c', directory / 'lifetime'
        cfile.write_text(preamble + actual + main)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', str(cfile), '-o', str(binary), *flags], check=True)
        result = subprocess.run([str(binary)], timeout=10)
        assert result.returncode == (0 if patched else 17), (patched, result.returncode)
print('PASS: original dispatcher releases stream too soon; patched dispatcher preserves lifetime for 1000 closes')
