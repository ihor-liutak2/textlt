-- SQL lexer sample
SELECT id, name, count(*)
FROM users
WHERE active = true AND created_at > '2026-01-01'
GROUP BY id, name;
