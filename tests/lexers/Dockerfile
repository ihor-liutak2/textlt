# Dockerfile lexer sample
FROM ubuntu:24.04
ARG APP_HOME=/app
WORKDIR "$APP_HOME"
RUN echo "installing" && apt-get update
COPY . .
CMD ["./textlt"]
