import { createRemoteJWKSet, jwtVerify, type JWTPayload } from "jose";

export interface AccessEnv {
  ACCESS_AUD?: string;
  ACCESS_TEAM_DOMAIN?: string;
  ADMIN_EMAILS?: string;
}

type AccessTokenVerifier = (
  token: string,
  teamDomain: string,
  audience: string,
) => Promise<JWTPayload>;

const remoteKeySets = new Map<string, ReturnType<typeof createRemoteJWKSet>>();

async function verifyCloudflareToken(
  token: string,
  teamDomain: string,
  audience: string,
): Promise<JWTPayload> {
  let keySet = remoteKeySets.get(teamDomain);
  if (!keySet) {
    keySet = createRemoteJWKSet(new URL(`${teamDomain}/cdn-cgi/access/certs`));
    remoteKeySets.set(teamDomain, keySet);
  }

  const { payload } = await jwtVerify(token, keySet, {
    algorithms: ["RS256"],
    audience,
    issuer: teamDomain,
  });
  return payload;
}

export async function authenticatedAccessUser(
  request: Request,
  env: AccessEnv,
  verifyToken: AccessTokenVerifier = verifyCloudflareToken,
): Promise<string | null> {
  const teamDomain = normalizedTeamDomain(env.ACCESS_TEAM_DOMAIN);
  const audience = env.ACCESS_AUD?.trim();
  const allowedEmails = configuredEmails(env.ADMIN_EMAILS);
  const token = request.headers.get("cf-access-jwt-assertion")?.trim();

  // Hosted administration deliberately supports exactly one person. Missing
  // or ambiguous configuration fails closed before any remote key lookup.
  if (!teamDomain || !audience || allowedEmails.length !== 1 || !token) {
    return null;
  }

  try {
    const payload = await verifyToken(token, teamDomain, audience);
    const email = typeof payload.email === "string" ? payload.email.trim().toLowerCase() : "";
    const accessHeader =
      request.headers.get("cf-access-authenticated-user-email")?.trim().toLowerCase();

    if (!email || email !== allowedEmails[0] || (accessHeader && accessHeader !== email)) {
      return null;
    }
    return email;
  } catch {
    return null;
  }
}

function normalizedTeamDomain(value: string | undefined): string | null {
  const candidate = value?.trim().replace(/\/+$/, "");
  if (!candidate) return null;

  try {
    const url = new URL(candidate);
    if (
      url.protocol !== "https:" ||
      url.username ||
      url.password ||
      url.port ||
      url.pathname !== "/" ||
      url.search ||
      url.hash ||
      !url.hostname.endsWith(".cloudflareaccess.com")
    ) {
      return null;
    }
    return url.origin;
  } catch {
    return null;
  }
}

function configuredEmails(value: string | undefined): string[] {
  return (value ?? "")
    .split(",")
    .map((email) => email.trim().toLowerCase())
    .filter(Boolean);
}
