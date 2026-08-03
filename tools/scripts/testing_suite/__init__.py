"""The Practice Takes testing suite.

A standalone application for verifying a Practice Takes build: it captures
every surface unattended, stores what it captured, and serves a grid for a
reviewer to tag, comment on, and score later.

Separate from the application on purpose. It has to verify a build it is not
part of, it must add nothing to the shipping binary, and it has to keep working
when the build under test does not.
"""
