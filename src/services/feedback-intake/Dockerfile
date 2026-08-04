FROM node:26-bookworm-slim AS build

WORKDIR /app

COPY package.json package-lock.json ./
COPY feedback-intake/package.json feedback-intake/package.json
RUN npm ci --workspace @practice-takes/feedback-intake

COPY feedback-intake/src feedback-intake/src
COPY feedback-intake/tsconfig.json feedback-intake/tsconfig.json

RUN npm run build:docker --workspace @practice-takes/feedback-intake

FROM node:26-bookworm-slim AS runtime

WORKDIR /app

COPY --from=build /app/feedback-intake/dist ./dist
COPY feedback-intake/docker-entrypoint.sh ./docker-entrypoint.sh

RUN chmod 755 docker-entrypoint.sh \
    && chown -R node:node /app

USER node

EXPOSE 8787

ENTRYPOINT ["./docker-entrypoint.sh"]
