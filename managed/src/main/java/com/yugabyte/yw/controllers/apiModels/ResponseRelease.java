package com.yugabyte.yw.controllers.apiModels;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.models.ReleaseArtifact;
import java.util.Date;
import java.util.List;
import java.util.UUID;

public class ResponseRelease {
  public UUID release_uuid;
  public String version;
  public String yb_type;
  public String release_type;
  public String state;

  public static class Artifact {
    public UUID package_file_id;
    public String file_name;
    public String package_url;
    public ReleaseArtifact.Platform platform;

    @JsonInclude(JsonInclude.Include.ALWAYS)
    public PublicCloudConstants.Architecture architecture;
  }

  public List<Artifact> artifacts;

  public String release_date;
  public String release_notes;
  public String release_tag;

  public class Universe {
    UUID uuid;
    String name;
    Date creation_date;
  }

  public List<Universe> universes;
}
