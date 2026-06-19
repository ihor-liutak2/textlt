#!/usr/bin/env bash
# Bash lexer sample.
export APP_ENV="dev"
if [ "$APP_ENV" = "dev" ]; then
  echo "port=${PORT:-8080}"
fi
