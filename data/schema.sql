-- Timetable Inquiry System — reference schema
--
-- This file is DOCUMENTATION for the data shape we serialize to CSV. The
-- current runtime persists records to data/timetable.csv and data/users.csv
-- and keeps an in-memory mirror (vector<Course> / vector<User>) for query
-- speed (see server/database.h). The assignment (IV.1) allows either
-- file-based storage or an embedded database; we chose CSV for portability
-- and inspectability, and this DDL describes the equivalent SQLite schema
-- that a future swap would target.

PRAGMA foreign_keys = ON;

-- ---------------------------------------------------------------------------
-- courses: one row per (code, section). Mirrors timetable.csv columns.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS courses (
    code        TEXT    NOT NULL,                  -- e.g. 'COMP3003'
    title       TEXT    NOT NULL,                  -- e.g. 'Data Communications'
    section     TEXT    NOT NULL,                  -- e.g. 'S1'
    instructor  TEXT    NOT NULL,                  -- e.g. 'Dr. Chan'
    day         TEXT    NOT NULL                   -- one of Mon..Fri
                CHECK (day IN ('Mon','Tue','Wed','Thu','Fri')),
    time        TEXT    NOT NULL                   -- 'HH:MM' (24h)
                CHECK (length(time) = 5 AND substr(time,3,1) = ':'),
    duration    TEXT    NOT NULL,                  -- e.g. '2h', '1.5h'
    classroom   TEXT    NOT NULL,                  -- e.g. 'A101'
    semester    TEXT    NOT NULL,                  -- e.g. '2026S1'
    PRIMARY KEY (code, section)
);

CREATE INDEX IF NOT EXISTS idx_courses_instructor ON courses (instructor);
CREATE INDEX IF NOT EXISTS idx_courses_semester   ON courses (semester);
CREATE INDEX IF NOT EXISTS idx_courses_day_time   ON courses (day, time);

-- ---------------------------------------------------------------------------
-- users: authentication table. Mirrors users.csv.
-- The 'password' column stores the credential; the server's authenticate()
-- accepts either a 64-char SHA-256 hex digest or plaintext, comparing
-- digests when the inbound value is 64 hex chars (see protocol.md §5.1).
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS users (
    username    TEXT    PRIMARY KEY,
    password    TEXT    NOT NULL,                  -- plaintext or SHA-256 hex
    role        TEXT    NOT NULL
                CHECK (role IN ('admin','student'))
);

-- Seed data (matches the bootstrap rows in server/database.h::createSample*).
INSERT OR IGNORE INTO users (username, password, role) VALUES
    ('admin',   'admin123', 'admin'),
    ('student', 'stu123',   'student'),
    ('alice',   'alice456', 'student'),
    ('bob',     'bob789',   'student');
