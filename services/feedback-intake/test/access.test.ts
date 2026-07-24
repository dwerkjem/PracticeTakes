import { describe, expect, it, vi } from "vitest";

import { authenticatedAccessUser, type AccessEnv } from "../src/access";

const teamDomain = "https://practice-takes.cloudflareaccess.com";
const audience = "feedback-dashboard-audience";
const email = "developer@example.com";

function environment(overrides: Partial<AccessEnv> = {}): AccessEnv {
  return {
    ACCESS_AUD: audience,
    ACCESS_TEAM_DOMAIN: teamDomain,
    ADMIN_EMAILS: email,
    ...overrides,
  };
}

function accessRequest(
  token: string | null = "signed-access-token",
  emailHeader: string | null = email,
): Request {
  const headers = new Headers();
  if (token) headers.set("cf-access-jwt-assertion", token);
  if (emailHeader) headers.set("cf-access-authenticated-user-email", emailHeader);
  return new Request("https://feedback.example.test/admin", { headers });
}

describe("Cloudflare Access authentication", () => {
  it("accepts a verified token for the one configured administrator", async () => {
    const verifier = vi.fn(async (
      token: string,
      issuer: string,
      expectedAudience: string,
    ) => {
      expect(token).toBe("signed-access-token");
      expect(issuer).toBe(teamDomain);
      expect(expectedAudience).toBe(audience);
      return { email: "Developer@Example.com" };
    });

    await expect(
      authenticatedAccessUser(accessRequest(), environment(), verifier),
    ).resolves.toBe(email);
    expect(verifier).toHaveBeenCalledOnce();
  });

  it("normalizes an adversarially long trailing-slash suffix in linear time", async () => {
    const verifier = vi.fn(async (
      _token: string,
      issuer: string,
    ) => {
      expect(issuer).toBe(teamDomain);
      return { email };
    });

    await expect(
      authenticatedAccessUser(
        accessRequest(),
        environment({ ACCESS_TEAM_DOMAIN: `${teamDomain}${"/".repeat(100_000)}` }),
        verifier,
      ),
    ).resolves.toBe(email);
    expect(verifier).toHaveBeenCalledOnce();
  });

  it("does not trust a caller-supplied identity header without an Access JWT", async () => {
    const verifier = vi.fn(async () => ({ email }));

    await expect(
      authenticatedAccessUser(accessRequest(null), environment(), verifier),
    ).resolves.toBeNull();
    expect(verifier).not.toHaveBeenCalled();
  });

  it("rejects a valid token belonging to another identity", async () => {
    const verifier = vi.fn(async () => ({ email: "attacker@example.com" }));

    await expect(
      authenticatedAccessUser(accessRequest(), environment(), verifier),
    ).resolves.toBeNull();
  });

  it("rejects an identity header that disagrees with the signed token", async () => {
    const verifier = vi.fn(async () => ({ email }));

    await expect(
      authenticatedAccessUser(
        accessRequest("signed-access-token", "attacker@example.com"),
        environment(),
        verifier,
      ),
    ).resolves.toBeNull();
  });

  it("fails closed unless exactly one administrator is configured", async () => {
    const verifier = vi.fn(async () => ({ email }));

    await expect(
      authenticatedAccessUser(
        accessRequest(),
        environment({ ADMIN_EMAILS: `${email},second@example.com` }),
        verifier,
      ),
    ).resolves.toBeNull();
    expect(verifier).not.toHaveBeenCalled();
  });

  it("fails closed for invalid Access configuration or token verification", async () => {
    const verifier = vi.fn(async () => {
      throw new Error("invalid signature");
    });

    await expect(
      authenticatedAccessUser(
        accessRequest(),
        environment({ ACCESS_TEAM_DOMAIN: "https://example.com" }),
        verifier,
      ),
    ).resolves.toBeNull();
    expect(verifier).not.toHaveBeenCalled();

    await expect(
      authenticatedAccessUser(accessRequest(), environment(), verifier),
    ).resolves.toBeNull();
    expect(verifier).toHaveBeenCalledOnce();
  });
});
