INSERT INTO members (id, name, grade, class, age) VALUES (1, 'Alice', 'vip', 'advanced', 30);
INSERT INTO members (id, name, grade, class, age) VALUES (2, 'Bob', 'normal', 'basic', 22);
SELECT * FROM members;
SELECT id, name FROM members WHERE age >= 25;
SELECT * FROM members ORDER BY age DESC;
